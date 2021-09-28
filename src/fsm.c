#include "fsm.h"
#include "aux.h"
#include "error.h"
#include "gff/aux.h"
#include "gff/elem.h"
#include "gff/tok.h"
#include "tok.h"
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

struct args
{
    struct gff_tok *tok;
    enum state state;
    struct gff_aux *aux;
    struct gff_elem *elem;
};

struct trans
{
    enum state const next;
    enum gff_rc (*action)(struct args *a);
};

static enum gff_rc nop(struct args *a) { return GFF_SUCCESS; }

static enum gff_rc unexpect_eof(struct args *a)
{
    return error_parse(a->tok->error, a->tok->line.number,
                       "unexpected end-of-file");
}

static enum gff_rc unexpect_tok(struct args *a)
{
    return error_parse(a->tok->error, a->tok->line.number, "unexpected token");
}

static enum gff_rc unexpect_pragma(struct args *a)
{
    return error_parse(a->tok->error, a->tok->line.number,
                       "unexpected directive");
}

static enum gff_rc unexpect_version(struct args *a)
{
    return error_parse(a->tok->error, a->tok->line.number,
                       "unexpected version directive");
}

static enum gff_rc unexpect_region(struct args *a)
{
    return error_parse(a->tok->error, a->tok->line.number,
                       "unexpected region directive");
}

static enum gff_rc unexpect_id(struct args *a)
{
    return error_parse(a->tok->error, a->tok->line.number, "unexpected id");
}

static enum gff_rc unexpect_nl(struct args *a)
{
    return error_parse(a->tok->error, a->tok->line.number,
                       "unexpected newline");
}

static enum gff_rc read_version(struct args *a);
static enum gff_rc read_region(struct args *a);
static enum gff_rc read_feat_seqid(struct args *a);
static enum gff_rc read_feat_source(struct args *a);
static enum gff_rc read_feat_type(struct args *a);
static enum gff_rc read_feat_start(struct args *a);
static enum gff_rc read_feat_end(struct args *a);
static enum gff_rc read_feat_score(struct args *a);
static enum gff_rc read_feat_strand(struct args *a);
static enum gff_rc read_feat_phase(struct args *a);
static enum gff_rc read_feat_attrs(struct args *a);
static enum gff_rc set_version_type(struct args *a);
static enum gff_rc set_region_type(struct args *a);
static enum gff_rc set_feature_type(struct args *a);

static struct trans const transition[][6] = {
    [STATE_BEGIN] = {[TOK_NL] = {STATE_ERROR, &unexpect_nl},
                     [TOK_PRAGMA] = {STATE_ERROR, &unexpect_pragma},
                     [TOK_VERSION] = {STATE_VERSION, &nop},
                     [TOK_REGION] = {STATE_REGION, &unexpect_region},
                     [TOK_WORD] = {STATE_ERROR, &unexpect_tok},
                     [TOK_EOF] = {STATE_END, &nop}},
    [STATE_VERSION] = {[TOK_NL] = {STATE_ERROR, &unexpect_nl},
                       [TOK_PRAGMA] = {STATE_ERROR, &unexpect_pragma},
                       [TOK_VERSION] = {STATE_ERROR, &unexpect_version},
                       [TOK_REGION] = {STATE_ERROR, &unexpect_region},
                       [TOK_WORD] = {STATE_VERSION_NL, &read_version},
                       [TOK_EOF] = {STATE_ERROR, &unexpect_eof}},
    [STATE_VERSION_NL] = {[TOK_NL] = {STATE_PAUSE, &set_version_type},
                          [TOK_PRAGMA] = {STATE_ERROR, &unexpect_pragma},
                          [TOK_VERSION] = {STATE_ERROR, &unexpect_version},
                          [TOK_REGION] = {STATE_ERROR, &unexpect_region},
                          [TOK_WORD] = {STATE_ERROR, &unexpect_tok},
                          [TOK_EOF] = {STATE_ERROR, &unexpect_eof}},
    [STATE_REGION] = {[TOK_NL] = {STATE_ERROR, &unexpect_nl},
                      [TOK_PRAGMA] = {STATE_ERROR, &unexpect_pragma},
                      [TOK_VERSION] = {STATE_ERROR, &unexpect_version},
                      [TOK_REGION] = {STATE_ERROR, &unexpect_region},
                      [TOK_WORD] = {STATE_REGION_NL, &read_region},
                      [TOK_EOF] = {STATE_ERROR, &unexpect_eof}},
    [STATE_REGION_NL] = {[TOK_NL] = {STATE_PAUSE, &set_region_type},
                         [TOK_PRAGMA] = {STATE_ERROR, &unexpect_pragma},
                         [TOK_VERSION] = {STATE_ERROR, &unexpect_version},
                         [TOK_REGION] = {STATE_ERROR, &unexpect_region},
                         [TOK_WORD] = {STATE_ERROR, &unexpect_tok},
                         [TOK_EOF] = {STATE_ERROR, &unexpect_eof}},
    [STATE_FEAT_SOURCE] = {[TOK_NL] = {STATE_ERROR, &unexpect_nl},
                           [TOK_PRAGMA] = {STATE_ERROR, &unexpect_pragma},
                           [TOK_VERSION] = {STATE_ERROR, &unexpect_version},
                           [TOK_REGION] = {STATE_ERROR, &unexpect_region},
                           [TOK_WORD] = {STATE_FEAT_TYPE, &read_feat_source},
                           [TOK_EOF] = {STATE_ERROR, &unexpect_eof}},
    [STATE_FEAT_TYPE] = {[TOK_NL] = {STATE_ERROR, &unexpect_nl},
                         [TOK_PRAGMA] = {STATE_ERROR, &unexpect_pragma},
                         [TOK_VERSION] = {STATE_ERROR, &unexpect_version},
                         [TOK_REGION] = {STATE_ERROR, &unexpect_region},
                         [TOK_WORD] = {STATE_FEAT_START, &read_feat_type},
                         [TOK_EOF] = {STATE_ERROR, &unexpect_eof}},
    [STATE_FEAT_START] = {[TOK_NL] = {STATE_ERROR, &unexpect_nl},
                          [TOK_PRAGMA] = {STATE_ERROR, &unexpect_pragma},
                          [TOK_VERSION] = {STATE_ERROR, &unexpect_version},
                          [TOK_REGION] = {STATE_ERROR, &unexpect_region},
                          [TOK_WORD] = {STATE_FEAT_END, &read_feat_start},
                          [TOK_EOF] = {STATE_ERROR, &unexpect_eof}},
    [STATE_FEAT_END] = {[TOK_NL] = {STATE_ERROR, &unexpect_nl},
                        [TOK_PRAGMA] = {STATE_ERROR, &unexpect_pragma},
                        [TOK_VERSION] = {STATE_ERROR, &unexpect_version},
                        [TOK_REGION] = {STATE_ERROR, &unexpect_region},
                        [TOK_WORD] = {STATE_FEAT_SCORE, &read_feat_end},
                        [TOK_EOF] = {STATE_ERROR, &unexpect_eof}},
    [STATE_FEAT_SCORE] = {[TOK_NL] = {STATE_ERROR, &unexpect_nl},
                          [TOK_PRAGMA] = {STATE_ERROR, &unexpect_pragma},
                          [TOK_VERSION] = {STATE_ERROR, &unexpect_version},
                          [TOK_REGION] = {STATE_ERROR, &unexpect_region},
                          [TOK_WORD] = {STATE_FEAT_STRAND, &read_feat_score},
                          [TOK_EOF] = {STATE_ERROR, &unexpect_eof}},
    [STATE_FEAT_STRAND] = {[TOK_NL] = {STATE_ERROR, &unexpect_nl},
                           [TOK_PRAGMA] = {STATE_ERROR, &unexpect_pragma},
                           [TOK_VERSION] = {STATE_ERROR, &unexpect_version},
                           [TOK_REGION] = {STATE_ERROR, &unexpect_region},
                           [TOK_WORD] = {STATE_FEAT_PHASE, &read_feat_strand},
                           [TOK_EOF] = {STATE_ERROR, &unexpect_eof}},
    [STATE_FEAT_PHASE] = {[TOK_NL] = {STATE_ERROR, &unexpect_nl},
                          [TOK_PRAGMA] = {STATE_ERROR, &unexpect_pragma},
                          [TOK_VERSION] = {STATE_ERROR, &unexpect_version},
                          [TOK_REGION] = {STATE_ERROR, &unexpect_region},
                          [TOK_WORD] = {STATE_FEAT_ATTRS, &read_feat_phase},
                          [TOK_EOF] = {STATE_ERROR, &unexpect_eof}},
    [STATE_FEAT_ATTRS] = {[TOK_NL] = {STATE_ERROR, &unexpect_nl},
                          [TOK_PRAGMA] = {STATE_ERROR, &unexpect_pragma},
                          [TOK_VERSION] = {STATE_ERROR, &unexpect_version},
                          [TOK_REGION] = {STATE_ERROR, &unexpect_region},
                          [TOK_WORD] = {STATE_FEAT_NL, &read_feat_attrs},
                          [TOK_EOF] = {STATE_ERROR, &unexpect_eof}},
    [STATE_FEAT_NL] = {[TOK_NL] = {STATE_PAUSE, &set_feature_type},
                       [TOK_PRAGMA] = {STATE_ERROR, &unexpect_pragma},
                       [TOK_VERSION] = {STATE_ERROR, &unexpect_version},
                       [TOK_REGION] = {STATE_ERROR, &unexpect_region},
                       [TOK_WORD] = {STATE_ERROR, &unexpect_tok},
                       [TOK_EOF] = {STATE_ERROR, &unexpect_eof}},
    [STATE_PAUSE] = {[TOK_NL] = {STATE_ERROR, &unexpect_nl},
                     [TOK_PRAGMA] = {STATE_ERROR, &unexpect_pragma},
                     [TOK_VERSION] = {STATE_ERROR, &unexpect_version},
                     [TOK_REGION] = {STATE_REGION, &nop},
                     [TOK_WORD] = {STATE_FEAT_SOURCE, &read_feat_seqid},
                     [TOK_EOF] = {STATE_ERROR, &unexpect_eof}},
    [STATE_END] = {[TOK_NL] = {STATE_ERROR, &unexpect_nl},
                   [TOK_PRAGMA] = {STATE_ERROR, &unexpect_id},
                   [TOK_VERSION] = {STATE_ERROR, &unexpect_version},
                   [TOK_REGION] = {STATE_ERROR, &unexpect_region},
                   [TOK_WORD] = {STATE_ERROR, &unexpect_tok},
                   [TOK_EOF] = {STATE_ERROR, &unexpect_eof}},
    [STATE_ERROR] = {[TOK_NL] = {STATE_ERROR, &nop},
                     [TOK_PRAGMA] = {STATE_ERROR, &nop},
                     [TOK_VERSION] = {STATE_ERROR, &unexpect_version},
                     [TOK_REGION] = {STATE_ERROR, &unexpect_region},
                     [TOK_WORD] = {STATE_ERROR, &nop},
                     [TOK_EOF] = {STATE_ERROR, &nop}},
};

static char state_name[][16] = {[STATE_BEGIN] = "BEGIN",
                                [STATE_VERSION] = "VERSION",
                                [STATE_VERSION_NL] = "VERSION_NL",
                                [STATE_REGION] = "REGION",
                                [STATE_REGION_NL] = "REGION_NL",
                                [STATE_FEAT_SOURCE] = "FEAT_SOURCE",
                                [STATE_FEAT_TYPE] = "FEAT_TYPE",
                                [STATE_FEAT_START] = "FEAT_START",
                                [STATE_FEAT_END] = "FEAT_END",
                                [STATE_FEAT_SCORE] = "FEAT_SCORE",
                                [STATE_FEAT_STRAND] = "FEAT_STRAND",
                                [STATE_FEAT_PHASE] = "FEAT_PHASE",
                                [STATE_FEAT_ATTRS] = "FEAT_ATTRS",
                                [STATE_FEAT_NL] = "FEAT_NL",
                                [STATE_PAUSE] = "PAUSE",
                                [STATE_END] = "END",
                                [STATE_ERROR] = "ERROR"};

enum state fsm_next(enum state state, struct gff_tok *tok, struct gff_aux *aux,
                    struct gff_elem *elem)
{
    unsigned row = (unsigned)state;
    unsigned col = (unsigned)tok->id;
    struct trans const *const t = &transition[row][col];
    struct args args = {tok, state, aux, elem};
    if (t->action(&args)) return STATE_ERROR;
    return t->next;
}

char const *fsm_name(enum state state) { return state_name[state]; }

static enum gff_rc tokcpy(char *dst, struct gff_tok *tok, size_t count,
                          char const *name);

static enum gff_rc read_version(struct args *a)
{
    assert(a->tok->id == TOK_WORD);
    return tokcpy(a->elem->version, a->tok, GFF_VERSION_SIZE, "version");
}

static enum gff_rc read_region(struct args *a)
{
    assert(a->tok->id == TOK_WORD);
    struct gff_region *r = &a->elem->region;
    gff_region_init(r);

    enum gff_rc rc = tokcpy(r->buffer, a->tok, GFF_REGION_SIZE, "region");
    if (rc) return rc;

    char *pos = strchr(r->name, ' ');
    if (pos == NULL)
        return error_parse(a->tok->error, a->tok->line.number, "missing space");
    r->start = pos + 1;

    pos = strchr(r->start, ' ');
    if (pos == NULL)
        return error_parse(a->tok->error, a->tok->line.number, "missing space");
    r->end = pos + 1;

    return rc;
}

static enum gff_rc set_version_type(struct args *a)
{
    assert(a->tok->id == TOK_NL);
    a->elem->type = GFF_VERSION;
    return GFF_SUCCESS;
}

static enum gff_rc set_region_type(struct args *a)
{
    assert(a->tok->id == TOK_NL);
    a->elem->type = GFF_REGION;
    return GFF_SUCCESS;
}

static enum gff_rc set_feature_type(struct args *a)
{
    assert(a->tok->id == TOK_NL);
    a->elem->type = GFF_FEATURE;
    return GFF_SUCCESS;
}

static enum gff_rc read_feat_seqid(struct args *a)
{
    assert(a->tok->id == TOK_WORD);
    struct gff_feature *f = &a->elem->feature;
    return tokcpy(f->seqid, a->tok, GFF_FEATURE_SEQID_SIZE, "seqid");
}

static enum gff_rc read_feat_source(struct args *a)
{
    assert(a->tok->id == TOK_WORD);
    struct gff_feature *f = &a->elem->feature;
    return tokcpy(f->source, a->tok, GFF_FEATURE_SOURCE_SIZE, "source");
}

static enum gff_rc read_feat_type(struct args *a)
{
    assert(a->tok->id == TOK_WORD);
    struct gff_feature *f = &a->elem->feature;
    return tokcpy(f->type, a->tok, GFF_FEATURE_TYPE_SIZE, "type");
}

static enum gff_rc read_feat_start(struct args *a)
{
    assert(a->tok->id == TOK_WORD);
    struct gff_feature *f = &a->elem->feature;
    return tokcpy(f->start, a->tok, GFF_FEATURE_START_SIZE, "start");
}

static enum gff_rc read_feat_end(struct args *a)
{
    assert(a->tok->id == TOK_WORD);
    struct gff_feature *f = &a->elem->feature;
    return tokcpy(f->end, a->tok, GFF_FEATURE_END_SIZE, "end");
}

static enum gff_rc read_feat_score(struct args *a)
{
    assert(a->tok->id == TOK_WORD);
    struct gff_feature *f = &a->elem->feature;
    return tokcpy(f->score, a->tok, GFF_FEATURE_SCORE_SIZE, "score");
}

static enum gff_rc read_feat_strand(struct args *a)
{
    assert(a->tok->id == TOK_WORD);
    struct gff_feature *f = &a->elem->feature;
    return tokcpy(f->strand, a->tok, GFF_FEATURE_STRAND_SIZE, "strand");
}

static enum gff_rc read_feat_phase(struct args *a)
{
    assert(a->tok->id == TOK_WORD);
    struct gff_feature *f = &a->elem->feature;
    return tokcpy(f->phase, a->tok, GFF_FEATURE_PHASE_SIZE, "phase");
}

static enum gff_rc read_feat_attrs(struct args *a)
{
    assert(a->tok->id == TOK_WORD);
    struct gff_feature *f = &a->elem->feature;
    return tokcpy(f->attrs, a->tok, GFF_FEATURE_ATTRS_SIZE, "attributes");
}

static enum gff_rc tokcpy(char *dst, struct gff_tok *tok, size_t count,
                          char const *name)
{
    char const *ptr = memccpy(dst, tok->value, '\0', count);
    if (!ptr)
        return error_parse(tok->error, tok->line.number, "too long %s", name);
    if (ptr - dst == 1)
        return error_parse(tok->error, tok->line.number, "empty %s", name);
    return GFF_SUCCESS;
}
