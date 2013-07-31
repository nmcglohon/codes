#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include "codes/configuration.h"

/*
 * Global to hold configuration in memory
 */
ConfigHandle config;

int configuration_load (const char *filepath,
                        MPI_Comm comm,
                        ConfigHandle *handle)
{
    MPI_File   fh;
    MPI_Status status;
    MPI_Offset txtsize;
    FILE      *f;
    char      *txtdata;
    char      *error;
    int        rc;

    rc = MPI_File_open(comm, (char*)filepath, MPI_MODE_RDONLY, MPI_INFO_NULL, &fh);
    assert(rc == MPI_SUCCESS);

    rc = MPI_File_get_size(fh, &txtsize);
    assert(rc == MPI_SUCCESS);

    txtdata = malloc(txtsize);
    assert(txtdata); 

    rc = MPI_File_read_all(fh, txtdata, txtsize, MPI_BYTE, &status);
    assert(rc == MPI_SUCCESS);

    rc = MPI_File_close(&fh);
    assert(rc == MPI_SUCCESS);

    f = fmemopen(txtdata, txtsize, "rb");
    assert(f);

    *handle = txtfile_openStream(f, &error);
    if (error)
    {
        fprintf(stderr, "config error: %s\n", error);
        free(error);
        rc = 1;
    }
    else
    {
        rc = 0;
    }

    fclose(f);

    return rc;
}

int configuration_get_value(ConfigHandle *handle,
                            const char *section_name,
                            const char *key_name,
                            char *value,
                            size_t len)
{
    SectionHandle section_handle;
    int           rc;

    rc = cf_openSection(*handle, ROOT_SECTION, section_name, &section_handle);
    assert(rc == 1);

    rc = cf_getKey(*handle, section_handle, key_name, value, len);
    assert(rc);

    (void) cf_closeSection(*handle, section_handle);

    return rc;
}

int configuration_get_value_int (ConfigHandle *handle,
                                 const char *section_name,
                                 const char *key_name,
                                 int *value)
{
    char valuestr[256];
    int rc = 1;
    int r;

    r = configuration_get_value(handle,
                                section_name,
                                key_name,
                                valuestr,
                                sizeof(valuestr));
    if (r > 0)
    {
        *value = atoi(valuestr);
        rc = 0;
    }

    return rc;
}

int configuration_get_value_uint (ConfigHandle *handle,
                                  const char *section_name,
                                  const char *key_name,
                                  unsigned int *value)
{
    char valuestr[256];
    int rc = 1;
    int r;

    r = configuration_get_value(handle,
                                section_name,
                                key_name,
                                valuestr,
                                sizeof(valuestr));
    if (r > 0)
    {
        *value = (unsigned int) atoi(valuestr);
        rc = 0;
    }

    return rc;
}

int configuration_get_value_longint (ConfigHandle *handle,
                                     const char *section_name,
                                     const char *key_name,
                                     long int *value)
{
    char valuestr[256];
    int rc = 1;
    int r;

    r = configuration_get_value(handle,
                                section_name,
                                key_name,
                                valuestr,
                                sizeof(valuestr));
    if (r > 0)
    {
        errno = 0;
        *value = strtol(valuestr, NULL, 10);
        rc = errno;
    }

    return rc;
}

int configuration_get_value_double (ConfigHandle *handle,
                                    const char *section_name,
                                    const char *key_name,
                                    double *value)
{
    char valuestr[256];
    int rc = 1;
    int r;

    r = configuration_get_value(handle,
                                section_name,
                                key_name,
                                valuestr,
                                sizeof(valuestr));
    if (r > 0)
    {
        errno = 0;
        *value = strtod(valuestr, NULL);
        rc = errno;
    }

    return rc;
}

int configuration_get_lpgroups (ConfigHandle *handle,
                                const char *section_name,
                                config_lpgroups_t *lpgroups)
{
    SectionHandle sh;
    SectionHandle subsh;
    SectionEntry se[10];
    SectionEntry subse[10];
    size_t se_count = 10;
    size_t subse_count = 10;
    int i, j, lpt;
    char data[256];

    memset (lpgroups, 0, sizeof(*lpgroups));

    cf_openSection(*handle, ROOT_SECTION, section_name, &sh);
    cf_listSection(*handle, sh, se, &se_count); 

    for (i = 0; i < se_count; i++)
    {
        //printf("section: %s type: %d\n", se[i].name, se[i].type);
        if (se[i].type == SE_SECTION)
        {
            subse_count = 10;
            cf_openSection(*handle, sh, se[i].name, &subsh);
            cf_listSection(*handle, subsh, subse, &subse_count);
            strncpy(lpgroups->lpgroups[i].name, se[i].name,
                    CONFIGURATION_MAX_NAME);
            lpgroups->lpgroups[i].repetitions = 1;
            lpgroups->lpgroups_count++;
            for (j = 0, lpt = 0; j < subse_count; j++)
            {
                if (subse[j].type == SE_KEY)
                {
                   cf_getKey(*handle, subsh, subse[j].name, data, sizeof(data));
                   //printf("key: %s value: %s\n", subse[j].name, data);
                   if (strcmp("repetitions", subse[j].name) == 0)
                   {
                       lpgroups->lpgroups[i].repetitions = atoi(data);
		       //printf("\n Repetitions: %ld ", lpgroups->lpgroups[i].repetitions);
                   }
                   else
                   {
                       // assume these are lptypes and counts
                       strncpy(lpgroups->lpgroups[i].lptypes[lpt].name,
                               subse[j].name,
                               sizeof(lpgroups->lpgroups[i].lptypes[lpt].name));
                       lpgroups->lpgroups[i].lptypes[lpt].count = atoi(data);
                       lpgroups->lpgroups[i].lptypes_count++;
                       lpt++;
                   }
                }
            }
            cf_closeSection(*handle, subsh);
        }
    }

    cf_closeSection(*handle, sh);
    
    return 0;
}
