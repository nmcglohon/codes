/*
 * Copyright (C) 2013 University of Chicago.
 * See COPYRIGHT notice in top-level directory.
 *
 */

/* Recorder workload generator that plugs into the general CODES workload
 * generator API. This generator consumes a set of input files of Recorder I/O
 * traces and passes these traces to the underlying simulator.
 */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>
#include <dirent.h>

#include "ross.h"
#include "codes/codes-workload.h"
#include "codes-workload-method.h"
#include "codes/quickhash.h"

#define RECORDER_MAX_TRACE_READ_COUNT 1024

#define RANK_HASH_TABLE_SIZE 397

struct recorder_io_op
{
    double start_time;
    struct codes_workload_op codes_op;
};

/* structure for storing all context needed to retrieve traces for this rank */
struct rank_traces_context
{
    int rank;
    struct qhash_head hash_link;

    struct recorder_io_op trace_ops[1024]; /* TODO: this should be extendable */
    int trace_list_ndx;
    int trace_list_max;
};


/* CODES workload API functions for workloads generated from recorder traces*/
static int recorder_io_workload_load(const char *params, int rank);
static void recorder_io_workload_get_next(int rank, struct codes_workload_op *op);

/* helper functions for recorder workload CODES API */
static int hash_rank_compare(void *key, struct qhash_head *link);

/* workload method name and function pointers for the CODES workload API */
struct codes_workload_method recorder_io_workload_method =
{
    .method_name = "recorder_io_workload",
    .codes_workload_load = recorder_io_workload_load,
    .codes_workload_get_next = recorder_io_workload_get_next,
};

static struct qhash_table *rank_tbl = NULL;
static int rank_tbl_pop = 0;

/* load the workload generator for this rank, given input params */
static int recorder_io_workload_load(const char *params, int rank)
{
    recorder_params *r_params = (recorder_params *) params;

    int64_t nprocs = 0;
    struct rank_traces_context *new = NULL;

    char *trace_dir = r_params->trace_dir_path;
    if(!trace_dir)
        return -1;

    /* allocate a new trace context for this rank */
    new = malloc(sizeof(*new));
    if(!new)
        return -1;

    new->rank = rank;
    new->trace_list_ndx = 0;
    new->trace_list_max = 0;

    DIR *dirp;
    struct dirent *entry;
    dirp = opendir(trace_dir);
    while((entry = readdir(dirp)) != NULL) {
        if(entry->d_type == DT_REG)
            nprocs++;
    }
    closedir(dirp);

    char trace_file_name[1024] = {'\0'};
    sprintf(trace_file_name, "%s/log.%d", trace_dir, rank);

    FILE *trace_file = fopen(trace_file_name, "r");
    if(trace_file == NULL)
        return -1;

    double start_time;
    char function_name[128] = {'\0'};

    /* Read the first chunk of data (of size RECORDER_MAX_TRACE_READ_COUNT) */
    char *line = NULL;
    size_t len;
    ssize_t ret_value;
    while((ret_value = getline(&line, &len, trace_file)) != -1) {
        struct recorder_io_op r_op;
        char *token = strtok(line, ", ");
        start_time = atof(token);
        token = strtok(NULL, ", ");
        strcpy(function_name, token);

        r_op.start_time = start_time;
        if(!strcmp(function_name, "open") || !strcmp(function_name, "open64")) {
            r_op.codes_op.op_type = CODES_WK_OPEN;

            token = strtok(NULL, ", (");
            token = strtok(NULL, ", )");
            r_op.codes_op.u.open.create_flag = atoi(token);

            token = strtok(NULL, ", )");
            token = strtok(NULL, ", ");
            r_op.codes_op.u.open.file_id = atoi(token);
        }
        else if(!strcmp(function_name, "close")) {
            r_op.codes_op.op_type = CODES_WK_CLOSE;

            token = strtok(NULL, ", ()");
            r_op.codes_op.u.close.file_id = atoi(token);
        }
        else if(!strcmp(function_name, "read") || !strcmp(function_name, "read64")) {
            r_op.codes_op.op_type = CODES_WK_READ;

            token = strtok(NULL, ", (");
            r_op.codes_op.u.read.file_id = atoi(token);

            // Throw out the buffer
            token = strtok(NULL, ", ");

            token = strtok(NULL, ", )");
            r_op.codes_op.u.read.size = atol(token);

            token = strtok(NULL, ", )");
            r_op.codes_op.u.read.offset = atol(token);
        }
        else if(!strcmp(function_name, "write") || !strcmp(function_name, "write64")) {
            r_op.codes_op.op_type = CODES_WK_WRITE;

            token = strtok(NULL, ", (");
            r_op.codes_op.u.write.file_id = atoi(token);

            // Throw out the buffer
            token = strtok(NULL, ", ");

            token = strtok(NULL, ", )");
            r_op.codes_op.u.write.size = atol(token);

            token = strtok(NULL, ", )");
            r_op.codes_op.u.write.offset = atol(token);
        }
        else if(!strcmp(function_name, "MPI_Barrier")) {
            r_op.codes_op.op_type = CODES_WK_BARRIER;

            r_op.codes_op.u.barrier.count = nprocs;
            r_op.codes_op.u.barrier.root = 0;
        }
        else{
            continue;
        }

        new->trace_ops[new->trace_list_ndx++] = r_op;
        if (new->trace_list_ndx == 1024) break;
    }

    fclose(trace_file);

    /* reset ndx to 0 and set max to event count */
    /* now we can read all events by counting through array from 0 - max */
    new->trace_list_max = new->trace_list_ndx;
    new->trace_list_ndx = 0;

    /* initialize the hash table of rank contexts, if it has not been initialized */
    if (!rank_tbl) {
        rank_tbl = qhash_init(hash_rank_compare, quickhash_32bit_hash, RANK_HASH_TABLE_SIZE);

        if (!rank_tbl) {
            free(new);
            return -1;
        }
    }

    /* add this rank context to the hash table */
    qhash_add(rank_tbl, &(new->rank), &(new->hash_link));
    rank_tbl_pop++;

    return 0;
}

/* pull the next trace (independent or collective) for this rank from its trace context */
static void recorder_io_workload_get_next(int rank, struct codes_workload_op *op)
{
    struct qhash_head *hash_link = NULL;
    struct rank_traces_context *tmp = NULL;

    /* Find event context for this rank in the rank hash table */
    hash_link = qhash_search(rank_tbl, &rank);

    /* terminate the workload if there is no valid rank context */
    if(!hash_link) {

        op->op_type = CODES_WK_END;
        return;
    }

    tmp = qhash_entry(hash_link, struct rank_traces_context, hash_link);
    assert(tmp->rank == rank);

    if(tmp->trace_list_ndx == tmp->trace_list_max) {
        /* no more events -- just end the workload */
        op->op_type = CODES_WK_END;
        qhash_del(hash_link);
        free(tmp);

        rank_tbl_pop--;
        if(!rank_tbl_pop)
        {
            qhash_finalize(rank_tbl);
            rank_tbl = NULL;
        }
    }
    else {
        /* return the next event */
        /* TODO: Do I need to check for the delay like in Darshan? */
        *op = tmp->trace_ops[tmp->trace_list_ndx++].codes_op;
    }

    return;
}

static int hash_rank_compare(void *key, struct qhash_head *link)
{
    int *in_rank = (int *)key;
    struct rank_traces_context *tmp;

    tmp = qhash_entry(link, struct rank_traces_context, hash_link);
    if (tmp->rank == *in_rank)
        return 1;

    return 0;
}


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ft=c ts=8 sts=4 sw=4 expandtab
 */
