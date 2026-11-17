/*
 * rastertoxqx - CUPS filter for the HP LaserJet Pro M1132 MFP on macOS.
 *
 * Reads CUPS raster (8-bit grey or 1-bit black) from stdin or a file,
 * halftones it to 1-bit, and feeds it as PBM to the foo2xqx encoder, which
 * is compiled into this same binary (its main() is renamed foo2xqx_main).
 * No Ghostscript, no foomatic, and no exec() of another program: the encoder
 * runs in a forked child of this process, which keeps the CUPS sandbox happy.
 *
 * usage (CUPS calls it): rastertoxqx job user title copies options [file]
 */
#include <cups/cups.h>
#include <cups/raster.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern int foo2xqx_main(int argc, char *argv[]);

/* 8x8 clustered-dot screen at 45 degrees: about 106 lpi at 600 dpi. */
static unsigned char screen[8][8];

static void build_screen(void)
{
    double s[64];
    int order[64], i, j;

    for (i = 0; i < 64; i++) {
	double x = (i % 8) + 0.5, y = (i / 8) + 0.5;
	s[i] = cos(2 * M_PI * (x + y) / 8.0) + cos(2 * M_PI * (x - y) / 8.0);
	order[i] = i;
    }
    for (i = 1; i < 64; i++) {		/* insertion sort, descending */
	int k = order[i];
	for (j = i - 1; j >= 0 && s[order[j]] < s[k]; j--)
	    order[j + 1] = order[j];
	order[j + 1] = k;
    }
    /* rank 0 (dot centre) turns black first; thresholds 2..254 */
    for (i = 0; i < 64; i++)
	screen[order[i] / 8][order[i] % 8] = (unsigned char)(254 - i * 4);
}

/* Paper codes foo2xqx understands, matched on PageSize in points. */
static const struct { int w, h, code, ux, uy; } papers[] = {
    { 612,  792,   1, 88, 84 },	/* Letter    */
    { 612, 1008,   5, 88, 96 },	/* Legal     */
    { 522,  756,   7, 96, 96 },	/* Executive */
    { 595,  842,   9, 88, 84 },	/* A4        */
    { 420,  595,  11, 96, 96 },	/* A5        */
    { 516,  729,  13, 96, 96 },	/* B5 (JIS)  */
    { 297,  684,  20, 85, 78 },	/* Env #10   */
    { 312,  624,  27, 88, 84 },	/* Env DL    */
    { 459,  649,  28, 85, 80 },	/* Env C5    */
    { 279,  540,  37, 87, 78 },	/* Monarch   */
};

int main(int argc, char *argv[])
{
    cups_raster_t *ras;
    cups_page_header2_t h;
    cups_option_t *opts = NULL;
    int nopts, fd = 0, pfd[2], page = 0, status = 0;
    pid_t pid = 0;
    FILE *out = NULL;
    unsigned char *line = NULL, *bits = NULL;
    const char *v;

    if (argc < 6 || argc > 7) {
	fprintf(stderr, "ERROR: usage: %s job user title copies options [file]\n", argv[0]);
	return 1;
    }
    if (argc == 7 && (fd = open(argv[6], O_RDONLY)) < 0) {
	perror("ERROR: cannot open raster file");
	return 1;
    }
    signal(SIGPIPE, SIG_IGN);
    build_screen();
    nopts = cupsParseOptions(argv[5], 0, &opts);

    if ((ras = cupsRasterOpen(fd, CUPS_RASTER_READ)) == NULL) {
	fprintf(stderr, "ERROR: cannot read raster stream\n");
	return 1;
    }

    while (cupsRasterReadHeader2(ras, &h)) {
	/*
	 * Apple's cgpdftoraster renders only the imageable area, so the
	 * raster (rw x rh) is smaller than the sheet.  Place it on a full
	 * sheet (w x hgt) at the offset given by the imaging bounding box,
	 * otherwise the image would be shifted towards the top-left corner.
	 */
	unsigned rw = h.cupsWidth, rh = h.cupsHeight, y, x;
	unsigned w = (unsigned)lround(h.PageSize[0] * h.HWResolution[0] / 72.0);
	unsigned hgt = (unsigned)lround(h.PageSize[1] * h.HWResolution[1] / 72.0);
	unsigned left, top, obpl;

	if (w < rw) w = rw;
	if (hgt < rh) hgt = rh;
	left = (unsigned)lround(h.ImagingBoundingBox[0] * h.HWResolution[0] / 72.0);
	top = h.PageSize[1] > h.ImagingBoundingBox[3] && h.ImagingBoundingBox[3] > 0
	    ? (unsigned)lround((h.PageSize[1] - h.ImagingBoundingBox[3]) * h.HWResolution[1] / 72.0)
	    : 0;
	if (left + rw > w) left = (w - rw) / 2;
	if (top + rh > hgt) top = (hgt - rh) / 2;
	obpl = (w + 7) / 8;

	if (h.cupsBitsPerPixel != 8 && h.cupsBitsPerPixel != 1) {
	    fprintf(stderr, "ERROR: unsupported raster depth %u\n", h.cupsBitsPerPixel);
	    status = 1;
	    break;
	}
	page++;

	if (page == 1) {
	    /* Start the encoder with options taken from the first page. */
	    char res[32], dim[32], pap[16], ul[32], lr[32], cop[16], den[16];
	    char *av[24];
	    int ac = 0, code = 1, ux = 88, uy = 84, density = 3, econo = 0;
	    unsigned i, copies = h.NumCopies ? h.NumCopies : 1;

	    for (i = 0; i < sizeof(papers) / sizeof(papers[0]); i++)
		if (abs((int)h.PageSize[0] - papers[i].w) <= 2 &&
		    abs((int)h.PageSize[1] - papers[i].h) <= 2) {
		    code = papers[i].code; ux = papers[i].ux; uy = papers[i].uy;
		    break;
		}
	    if (i == sizeof(papers) / sizeof(papers[0]))
		fprintf(stderr, "WARNING: unknown page size %ux%u pt, sending as Letter\n",
			h.PageSize[0], h.PageSize[1]);
	    if ((v = cupsGetOption("Density", nopts, opts)) != NULL &&
		atoi(v) >= 1 && atoi(v) <= 5)
		density = atoi(v);
	    if ((v = cupsGetOption("EconoMode", nopts, opts)) != NULL &&
		(!strcasecmp(v, "true") || !strcasecmp(v, "on")))
		econo = 1;

	    snprintf(res, sizeof res, "-r%ux%u", h.HWResolution[0], h.HWResolution[1]);
	    snprintf(dim, sizeof dim, "-g%ux%u", w, hgt);
	    snprintf(pap, sizeof pap, "-p%d", code);
	    snprintf(ul, sizeof ul, "-u%dx%d", ux * (int)h.HWResolution[0] / 600, uy);
	    snprintf(lr, sizeof lr, "-l%dx%d", ux * (int)h.HWResolution[0] / 600, uy);
	    snprintf(cop, sizeof cop, "-n%u", copies);
	    snprintf(den, sizeof den, "-T%d", density);
	    av[ac++] = "foo2xqx"; av[ac++] = res; av[ac++] = dim; av[ac++] = pap;
	    av[ac++] = "-m1"; av[ac++] = "-d1"; av[ac++] = "-s7";
	    av[ac++] = ul; av[ac++] = lr; av[ac++] = cop; av[ac++] = den;
	    if (econo) av[ac++] = "-t";
	    av[ac++] = "-J"; av[ac++] = argv[3];
	    av[ac++] = "-U"; av[ac++] = argv[2];
	    av[ac] = NULL;

	    fprintf(stderr, "INFO: rastertoxqx sheet %ux%u px, raster %ux%u at +%u+%u, %ux%u dpi, %u bpp, paper %d\n",
		    w, hgt, rw, rh, left, top, h.HWResolution[0], h.HWResolution[1],
		    h.cupsBitsPerPixel, code);

	    fflush(NULL);
	    if (pipe(pfd) < 0 || (pid = fork()) < 0) {
		perror("ERROR: cannot start encoder");
		return 1;
	    }
	    if (pid == 0) {
		dup2(pfd[0], 0);
		close(pfd[0]); close(pfd[1]);
		if (fd) close(fd);
		_exit(foo2xqx_main(ac, av) ? 1 : (fflush(stdout), 0));
	    }
	    close(pfd[0]);
	    out = fdopen(pfd[1], "w");
	}

	line = realloc(line, h.cupsBytesPerLine);
	bits = realloc(bits, obpl);
	fprintf(out, "P4\n%u %u\n", w, hgt);
	fprintf(stderr, "PAGE: %d %u\n", page, h.NumCopies ? h.NumCopies : 1);

	for (y = 0; y < hgt; y++) {
	    memset(bits, 0, obpl);
	    if (y < top || y >= top + rh) {		/* blank band above/below */
		if (fwrite(bits, 1, obpl, out) != obpl) { status = 1; goto done; }
		continue;
	    }
	    if (cupsRasterReadPixels(ras, line, h.cupsBytesPerLine) != h.cupsBytesPerLine) {
		fprintf(stderr, "ERROR: short raster data on page %d\n", page);
		status = 1;
		memset(line, h.cupsBitsPerPixel == 8 && h.cupsColorSpace != CUPS_CSPACE_K ? 255 : 0,
		       h.cupsBytesPerLine);
	    }
	    if (h.cupsBitsPerPixel == 1) {
		/* K: 1 = black already.  W: 1 = white, so invert. */
		int kspace = h.cupsColorSpace == CUPS_CSPACE_K;
		for (x = 0; x < rw; x++) {
		    int on = (line[x >> 3] >> (7 - (x & 7))) & 1;
		    if (on == kspace)
			bits[(x + left) >> 3] |= (unsigned char)(0x80 >> ((x + left) & 7));
		}
	    } else {
		const unsigned char *t = screen[y & 7];
		int kspace = h.cupsColorSpace == CUPS_CSPACE_K;
		for (x = 0; x < rw; x++) {
		    unsigned g = kspace ? 255u - line[x] : line[x];	/* 255 = white */
		    unsigned ox = x + left;
		    if (g < t[ox & 7])
			bits[ox >> 3] |= (unsigned char)(0x80 >> (ox & 7));
		}
	    }
	    if (fwrite(bits, 1, obpl, out) != obpl) {
		fprintf(stderr, "ERROR: encoder stopped reading\n");
		status = 1;
		goto done;
	    }
	}
    }

done:
    cupsRasterClose(ras);
    if (out) fclose(out);
    if (pid > 0) {
	int st;
	waitpid(pid, &st, 0);
	if (!WIFEXITED(st) || WEXITSTATUS(st)) {
	    fprintf(stderr, "ERROR: foo2xqx encoder failed\n");
	    status = 1;
	}
    }
    if (page == 0) {
	fprintf(stderr, "ERROR: no pages in raster stream\n");
	status = 1;
    }
    return status;
}
