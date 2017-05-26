#define BENCHMARK "OSU One Sided MPI_Put Bandwidth Test"
/*
 * Copyright (C) 2003-2011 the Network-Based Computing Laboratory
 * (NBCL), The Ohio State University.
 *
 * Contact: Dr. D. K. Panda (panda@cse.ohio-state.edu)
 */

/*
This program is available under BSD licensing.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

(1) Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

(2) Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

(3) Neither the name of The Ohio State University nor the names of
their contributors may be used to endorse or promote products derived
from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

#include "osu.h"
#include <assert.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

#define MAX_ALIGNMENT 65536
#define MAX_MSG_SIZE (1<<22)

/* Note we have a upper limit for buffer size, so be extremely careful
 * if you want to change the loop size or warm up size */
int loop = 100;
int window_size = 32;
int skip = 20;

int loop_large = 30;
int window_size_large = 32;
int skip_large = 10;

int large_message_size = 8192;

int main (int argc, char *argv[])
{
    int         myid, numprocs, i, j;
    int         size, page_size;
    char        *s_buf, *r_buf;
    char        *s_buf1, *r_buf1;
    double      t_start = 0.0, t_end = 0.0, t = 0.0;
    const char *fifo_path = "/tmp/test_fifo";
    int fd;
    struct iovec local;

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &numprocs);
    MPI_Comm_rank(MPI_COMM_WORLD, &myid);

    if (numprocs != 2) {
        if (myid == 0) {
            fprintf(stderr, "This test requires exactly two processes\n");
        }

        MPI_Finalize();

        return EXIT_FAILURE;
    }

    page_size = getpagesize();
    assert(page_size <= MAX_ALIGNMENT);

    s_buf1 = malloc(MAX_MSG_SIZE + MAX_ALIGNMENT);
    if (NULL == s_buf1) {
         fprintf(stderr, "[%d] Buffer Allocation Failed \n", myid);
         exit(-1);
    }
    r_buf1 = malloc(MAX_MSG_SIZE*window_size + MAX_ALIGNMENT);
    if (NULL == r_buf1) {
         fprintf(stderr, "[%d] Buffer Allocation Failed \n", myid);
         fflush(stdout);
         exit(-1);
    }

    s_buf = (char *) (((unsigned long) s_buf1 + (page_size - 1)) / page_size *
          page_size);
    r_buf = (char *) (((unsigned long) r_buf1 + (page_size - 1)) / page_size *
          page_size);

    assert((s_buf != NULL) && (r_buf != NULL));

    if (myid == 0) {
        int status;

        status = mkfifo(fifo_path, S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);
        if (status != 0)
           fprintf(stderr, "Error in mkfifo\n");

        fprintf(stdout, "# %s v%s\n", BENCHMARK, PACKAGE_VERSION);
        fprintf(stdout, "%-*s%*s\n", 10, "# Size", FIELD_WIDTH,
                "Bandwidth (MB/s)");
        fflush(stdout);
    }
    MPI_Barrier(MPI_COMM_WORLD);

    /* Open pipe */
    if (myid == 0) {
      fd = open(fifo_path, O_WRONLY);
   } else {
      fd = open(fifo_path, O_RDONLY);
    }
    if (fd < 0)
       fprintf(stderr, "Error in open\n");
 
    /* Bandwidth test */
    for (size = 1; size <= MAX_MSG_SIZE; size *= 2) {
        if (size > large_message_size) {
            loop = loop_large;
            skip = skip_large;
            window_size = window_size_large;
        }

        local.iov_len = size;

        if (myid == 0) {
            local.iov_base = s_buf;

            for (i = 0; i < skip; i++) {
                  size_t nwrite;

                  nwrite = writev(fd, &local, 1);
                  if (nwrite != size)
                          fprintf(stderr, "Error nwrite=%zd\n", nwrite);
            }
        } else {
            for (i = 0; i < skip; i++) {
                  size_t nread;

                  local.iov_base = r_buf + i * size;
                  nread = readv(fd, &local, 1);
                  if (nread != size)
                          fprintf(stderr, "Error nread=%zd\n", nread);
        
            }
        }

        MPI_Barrier(MPI_COMM_WORLD);

        if (myid == 0) {
            t_start = MPI_Wtime();

            local.iov_base = s_buf;

            for (i = 0; i < loop; i++) {

                for(j = 0; j < window_size; j++) {
                    size_t nwrite;

                    nwrite = writev(fd, &local, 1);
                    if (nwrite != size)
                            fprintf(stderr, "Error nwrite=%zd\n", nwrite);
                }
            }

            t_end = MPI_Wtime();
            t = t_end - t_start;
        } else {
            for (i = 0; i < loop; i++) {
               for(j = 0; j < window_size; j++) {
                  size_t nread;

                  local.iov_base = r_buf + j * size;
                  nread = readv(fd, &local, 1);
                  if (nread != size)
                          fprintf(stderr, "Error nread=%zd\n", nread);
               }
            }
        }

        MPI_Barrier(MPI_COMM_WORLD);

        if (myid == 0) {
            double tmp = size / 1e6 * loop * window_size;

            fprintf(stdout, "%-*d%*.*f\n", 10, size, FIELD_WIDTH,
                    FLOAT_PRECISION, tmp / t);
            fflush(stdout);
        }
    }

    MPI_Barrier(MPI_COMM_WORLD);
    close(fd);
    if (myid == 0)
        unlink(fifo_path);
    MPI_Finalize();

    return EXIT_SUCCESS;
}

/* vi: set sw=4 sts=4 tw=80: */
