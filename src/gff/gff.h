#ifndef GFF_GFF_H
#define GFF_GFF_H

#include "gff/aux.h"
#include "gff/error.h"
#include "gff/export.h"
#include "gff/feature.h"
#include "gff/rc.h"
#include "gff/region.h"
#include "gff/tok.h"
#include <stdio.h>

enum gff_mode
{
    GFF_READ,
    GFF_WRITE,
};

enum gff_elem
{
    GFF_UNKNOWN,
    GFF_VERSION,
    GFF_REGION,
    GFF_FEATURE,
};

#define GFF_VERSION_SIZE 16

struct gff
{
    FILE *restrict fd;
    enum gff_mode mode;
    enum gff_elem elem;
    char version[GFF_VERSION_SIZE];
    struct gff_region region;
    struct gff_feature feature;
    unsigned state;
    struct gff_tok tok;
    struct gff_aux aux;
    char error[GFF_ERROR_SIZE];
};

GFF_API void gff_init(struct gff *fa, FILE *restrict fd, enum gff_mode mode);

GFF_API enum gff_rc gff_read(struct gff *fa);

GFF_API void gff_clearerr(struct gff *fa);

GFF_API enum gff_rc gff_write(struct gff *fa, struct gff_target tgt,
                              unsigned ncols);

#endif