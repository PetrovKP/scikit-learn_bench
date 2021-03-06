/*
 * npyfile.h
 * a small implementation of reading numpy's output format.
 *
 * Copyright (C) 2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _NPYFILE_H_
#define _NPYFILE_H_

#define NPY_VERSION 0x0200
static const char NPY_HEADER[] = "\x93NUMPY";

struct npyarr {
    char *descr; /* dtype descriptor */
    bool fortran_order; /* is this array F-contiguous? */
    size_t shape_len; /* number of dimensions in shape */
    size_t *shape; /* shape of array */
    void *data; /* the actual array */
};


void free_npy(struct npyarr *arr) {
    if (arr != NULL) {
        if (arr->shape != NULL) {
            free(arr->shape);
        }
        if (arr->descr != NULL) {
            free(arr->descr);
        }
        if (arr->data != NULL) {
            free(arr->data);
        }
        free(arr);
    }
}


struct npyarr *load_npy(const char *path) {

    if (path == NULL) {
        return NULL;
    }

    /* open file */
    FILE *f;
    if ((f = fopen(path, "rb")) == NULL) {
        return NULL;
    }

    struct npyarr *arr = NULL;

    /* read magic numbers */
    for (unsigned int i = 0; i < 6; i++) {
        int c = fgetc(f);
        if (c == EOF || ((char) c) != NPY_HEADER[i]) {
            /* magic number mismatch */
            goto fail;
        }
    }

    /* read version */
    int version;
    int version_minor;
    if ((version = fgetc(f)) == EOF) {
        /* unexpected EOF */
        goto fail;
    }
    if ((version_minor = fgetc(f)) == EOF) {
        /* unexpected EOF */
        goto fail;
    }

    version = (version << 8) | version_minor;

    /* make sure we don't encounter a version we don't know */
    if (version > NPY_VERSION) {
        goto fail;
    }
    
    /* determine length of header length field */
    int header_len_len = 2;
    if (version >= 0x0200) {
        header_len_len = 4;
    }

    /* read header length field */
    unsigned int header_len = 0;
    for (unsigned int i = 0; i < header_len_len; i++) {
        int c = fgetc(f);
        if (c == EOF) {
            /* unexpected EOF */
            goto fail;
        }
        header_len = ((c & 0xFF) << (i * 8)) | header_len;
    }

    /* reading header dictionary */
    char key; /* first chars of keys are distinct! */
    char state = 0; /* treat this like a state machine */

    arr = (struct npyarr *) malloc(sizeof(*arr));
    arr->shape_len = 0;
    arr->shape = NULL;
    arr->descr = NULL;
    long shape_loc;
    unsigned int shape_i = 0;
    unsigned int descr_i = 0;
    unsigned int descr_len = 0;
    int last_c = 0;
    for (unsigned int i = 0; i < header_len; i++) {
        int c = fgetc(f);
        if (c == EOF) {
            /* unexpected EOF */
            goto fail;
        }

        switch (state) {
            case 0: /* outside dict */
                if (c != '{') {
                    /* expected dict to start here */
                    goto fail;
                }
                state = 'a';
                break;
            case 'a': /* inside dict, outside key */
                if (c == '\'' || c == '"') {
                    /* key starts here */
                    state = 'k';
                }
                if (c == '}') {
                    /* we're done with the dictionary */
                    state = 'z';
                }
                /* ignore all other characters here */
                break;
            case 'k': /* reading first character of key */
                key = c;
                state = 'l';
                break;
            case 'l': /* skip through rest of key */
                if (c == '\'' || c == '"') {
                    /* key ends here */
                    state = 'm';
                }
                break;
            case 'm': /* after end of key */
                if (c == ':') {
                    /* get ready for value */
                    state = 'v';
                    break;
                }
                break;
            case 'v': /* after colon */
                if (c == ' ' || c == '\t' || c == '\n') {
                    /* ignore whitespace */
                    break;
                }
                state = key;
                /* save current location */
                shape_loc = ftell(f);
                /* push back the current char */
                if (state == 'f') ungetc(c, f);
                break;
            case 'd': /* reading "descr" field, pass 1 */
                if (c == '\'' || c == '"') {
                    /* we're done here */
                    state = 'e';
                    fseek(f, shape_loc, SEEK_SET);
                    arr->descr = (char *) calloc(descr_len+1, sizeof(char));
                    descr_i = 0;
                    break;
                }
                descr_len++;
                break;
            case 'e': /* reading "descr" field, pass 2 */
                if (c == '\'' || c == '"') {
                    /* we're done here */
                    state = 'c';
                    break;
                }
                
                arr->descr[descr_i++] = c;
                break;
            case 'f': /* reading "fortran_order" field */
                arr->fortran_order = (c == 'T');
                state = 'c';
                break;
            case 's': /* reading "shape" field, pass 1 */
                if (c == ' ' || c == '\t' || c == '\n') {
                    /* ignore whitespace */
                    break;
                }

                if (c == ',') {
                    /* next element in shape */
                    last_c = c;
                    arr->shape_len++;
                }

                if (c >= '0' && c <= '9') {
                    last_c = c;
                }

                if (c == ')') {
                    if (last_c >= '0' && last_c <= '9') {
                        arr->shape_len++;
                    }
                    /* go to second pass */
                    state = 't';
                    fseek(f, shape_loc, SEEK_SET);
                    arr->shape = (size_t *) calloc(arr->shape_len, sizeof(*arr->shape));
                    shape_i = 0;
                }
                break;
            case 't': /* reading "shape" field, pass 2 */
                if (c == ' ' || c == '\t' || c == '\n') {
                    /* ignore whitespace */
                    break;
                }
                
                if (c >= '0' && c <= '9') {
                    arr->shape[shape_i] *= 10;
                    arr->shape[shape_i] += (c - '0');
                }

                if (c == ',') {
                    /* next element in shape */
                    shape_i++;
                }

                if (c == ')') {
                    /* done with shape */
                    state = 'c';
                }
                break;
            case 'c': /* waiting for comma for next k: v pair */
                if (c == ',') {
                    state = 'a';
                }
                
                if (c == '}') {
                    /* we're done with the dictionary */
                    state = 'z';
                }

                /* ignore everything else */
                break;
            case 'z':
                /* done with dictionary. now, just consume the rest */
                break;
            default:
                /* we should never get here! */
                goto fail;
        }
    }

    /* Keep reading until we pass a newline character */
    while (fgetc(f) != '\n');

    /* Read the array data now */
    off_t data_begin = ftell(f);
    fseek(f, 0, SEEK_END);
    off_t data_end = ftell(f);
    fseek(f, data_begin, SEEK_SET);
    arr->data = malloc(data_end - data_begin);

    fread(arr->data, 1, data_end - data_begin, f);
    fclose(f);

    return arr;

fail:
    fclose(f);
    free_npy(arr);
    return NULL;
}


/*
 * Write an array to disk.
 * N.B. elem_size must be the correct size of each element in the
 * array.
 */
void save_npy(const struct npyarr *arr, const char *path, size_t elem_size) {

    if (path == NULL || arr == NULL) {
        return;
    }

    /* open file */
    FILE *f;
    if ((f = fopen(path, "wb")) == NULL) {
        return;
    }

    /* Determine header_len */
    unsigned int header_len = 0;
    static const char header_1[] = "{'descr': '";
    static const char header_2[] = "', 'fortran_order': ";
    static const char header_3[] = ", 'shape': (";
    static const char header_4[] = ")}";
    header_len += strlen(header_1) + strlen(header_2);
    header_len += strlen(header_3) + strlen(header_4);

    header_len += strlen(arr->descr);
    header_len += (arr->fortran_order) ? 4 : 5;
    header_len += 1; /* the terminating newline */

    /* Determine shape length */
    unsigned int shape_len = 0;
    for (int i = 0; i < arr->shape_len; i++) {
        /* Number of digits in the number */
        if (arr->shape[i] > 0) {
            shape_len += floor(log10(arr->shape[i]) + 1);
        } else {
            shape_len += 1;
        }
        if (i < arr->shape_len - 1 && arr->shape_len != 1) {
            /* Adding commas where needed */
            shape_len += 2;
        }
    }

    header_len += shape_len;
    
    /* 16-byte alignment */
    unsigned int bytes_left = 16 - (strlen(NPY_HEADER) + 4 + header_len) % 16;
    header_len += bytes_left;

    unsigned char major_version = 0x01;
    if (header_len > 65535) {
        major_version = 0x02;
    }
    unsigned char minor_version = 0x00;

    fputs(NPY_HEADER, f);
    fputc(major_version, f);
    fputc(minor_version, f);
    
    /* writing header_len in little-endian format always */
    for (int i = 0; i < ((major_version >= 0x02) ? 4 : 2); i++) {
        fputc((header_len >> (i * 8)) & 0xff, f);
    }

    fputs(header_1, f);
    fputs(arr->descr, f);
    fputs(header_2, f);
    fputs((arr->fortran_order) ? "True" : "False", f);
    fputs(header_3, f);

    size_t nelem = 1;
    for (int i = 0; i < arr->shape_len; i++) {
        fprintf(f, "%d", arr->shape[i]);
        nelem *= arr->shape[i];
        if (i < arr->shape_len - 1 && arr->shape_len != 1) {
            fputs(", ", f);
        }
    }
    fputs(header_4, f);

    /* Pad with spaces */
    for (int i = 0; i < bytes_left; i++) {
        fputc(' ', f);
    }

    /* Add terminating newline */
    fputc('\n', f);

    /* Write array data */
    fwrite(arr->data, elem_size, nelem, f);

    /* done. */
    fflush(f);
    fclose(f);

}

#endif /* _NPYFILE_H_ */
