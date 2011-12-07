/* paper_databuf.c
 *
 * Routines for creating and accessing main data transfer
 * buffer in shared memory.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <errno.h>
#include <time.h>
#include <sys/resource.h> 

#include "fitshead.h"
#include "guppi_status.h"
#include "paper_databuf.h"
#include "guppi_error.h"

/*
 * Since the first element of struct paper_input_databuf is a struct
 * guppi_databuf, a pointer to a struct paper_input_databuf is also a pointer
 * to a struct guppi_databuf.  This allows a pointer to a struct
 * paper_input_databuf to be passed, with appropriate casting, to functions
 * that accept a pointer to a struct guppi_databuf.  This allows the reuse of
 * many of the functions in guppi_databuf.c.  This is a form of inheritence: a
 * struct paper_input_databuf is a struct guppi_databuf (plus additional
 * specializations).
 *
 * Many of the functions in guppi_databuf.c accept a pointer to a struct
 * guppi_databuf, but unfortunately some of them have VEGAS-specific code or
 * parameters which render them unsuitable for general use.
 *
 * For guppi_databuf.c function...
 *
 *   guppi_databuf_xyzzy(struct guppi_databuf *d...)
 *
 * ...that is suitable for general use, a corresponding paper_databuf.c
 * function...
 *
 *   paper_input_databuf_xyzzy(struct paper_input_databuf *d...)
 *
 * ...can be created that passes its d parameter to guppi_databuf_xyzzy with
 * appropraite casting.  In some cases (e.g. guppi_databuf_attach), that's all
 * that's needed, but other cases may require additional functionality in the
 * paper_input_buffer function.
 *
 * guppi_databuf.c functions that are not suitable for general use will have
 * to be duplicated in a paper-specific way (i.e. without calling the
 * guppi_databuf version) if they are in fact relevent to paper_input_databuf.
 * Functions that are duplicated for this reason should have a brief comment
 * indicating why they are bgin duplicated rather than simply calling the
 * guppi_databuf.c equivalent.
 *
 */

struct paper_input_databuf *paper_databuf_create(int n_block, size_t block_size,
        int databuf_id, int buf_type) {

    /* Calc databuf size */
    size_t guppi_databuf_header_size = sizeof(struct guppi_databuf);
printf("guppi_databuf_header_size %d\n", guppi_databuf_header_size);
    size_t paper_input_header_size = sizeof(paper_input_header_t);    
printf("paper_input_header_size %d\n", paper_input_header_size);
    size_t paper_input_block_size = sizeof(paper_input_block_t);
printf("paper_input_block_size %d\n", paper_input_block_size);
    size_t paper_input_databuf_size = sizeof(paper_input_databuf_t);
printf("paper_input_databuf_size %d\n", paper_input_databuf_size);
      size_t databuf_size = paper_input_databuf_size + 
                            (paper_input_block_size + NUM_HEADERS_PER_BLOCK * block_size) * n_block;
printf("databuf_size %d\n", databuf_size);
//exit(1);	// debug exit

    /* Get shared memory block, error if it already exists */
    int shmid;
    shmid = shmget(GUPPI_DATABUF_KEY + databuf_id - 1, 
            databuf_size, 0666 | IPC_CREAT | IPC_EXCL);
    if (shmid==-1) {
	perror("guppi_databuf_create()");
        guppi_error("guppi_databuf_create", "shmget error");
        return(NULL);
    }

    /* Attach */
    struct guppi_databuf *d;
    d = shmat(shmid, NULL, 0);
    if (d==(void *)-1) {
        guppi_error("guppi_databuf_create", "shmat error");
        return(NULL);
    }

    /* Try to lock in memory */
    int rv = shmctl(shmid, SHM_LOCK, NULL);
    if (rv==-1) {
	printf("errno %d\n", errno);
        guppi_error("guppi_databuf_create", "Error locking shared memory.");
        perror("shmctl");
    }

    /* Zero out memory */
    memset(d, 0, databuf_size);

    /* Fill params into databuf */
    int i;
    char end_key[81];
    memset(end_key, ' ', 80);
    strncpy(end_key, "END", 3);
    end_key[80]='\0';
    d->shmid = shmid;
    d->semid = 0;
    d->n_block = n_block;
    //d->struct_size = struct_size;
    d->block_size = block_size;
    //d->header_size = header_size;
    //d->index_size = index_size;
    //sprintf(d->data_type, "unknown");
    //d->buf_type = buf_type;

    for (i=0; i<n_block; i++) { 
        memcpy(guppi_databuf_header(d,i), end_key, 80); 
    }

    /* Get semaphores set up */
    d->semid = semget(GUPPI_DATABUF_KEY + databuf_id - 1, 
            n_block, 0666 | IPC_CREAT);
    if (d->semid==-1) { 
        guppi_error("guppi_databuf_create", "semget error");
        return(NULL);
    }

    /* Init semaphores to 0 */
    union semun arg;
    arg.array = (unsigned short *)malloc(sizeof(unsigned short)*n_block);
    memset(arg.array, 0, sizeof(unsigned short)*n_block);
    rv = semctl(d->semid, 0, SETALL, arg);
    free(arg.array);

    return(d);
}

int guppi_databuf_detach(struct guppi_databuf *d) {
    int rv = shmdt(d);
    if (rv!=0) {
        guppi_error("guppi_status_detach", "shmdt error");
        return(GUPPI_ERR_SYS);
    }
    return(GUPPI_OK);
}

void guppi_databuf_clear(struct guppi_databuf *d) {

    /* Zero out semaphores */
    union semun arg;
    arg.array = (unsigned short *)malloc(sizeof(unsigned short)*d->n_block);
    memset(arg.array, 0, sizeof(unsigned short)*d->n_block);
    semctl(d->semid, 0, SETALL, arg);
    free(arg.array);

    /* Clear all headers */
    int i;
    for (i=0; i<d->n_block; i++) {
        guppi_fitsbuf_clear(guppi_databuf_header(d, i));
    }

}

void guppi_fitsbuf_clear(char *buf) {
    char *end, *ptr;
    end = ksearch(buf, "END");
    if (end!=NULL) {
        for (ptr=buf; ptr<=end; ptr+=80) memset(ptr, ' ', 80);
    }
    memset(buf, ' ' , 80);
    strncpy(buf, "END", 3);
}

#ifndef NEW_GBT
char *guppi_databuf_header(struct guppi_databuf *d, int block_id) {
    return((char *)d + d->struct_size + block_id*d->header_size);
}

char *guppi_databuf_data(struct guppi_databuf *d, int block_id) {
    return((char *)d + d->struct_size + d->n_block*d->header_size
            + block_id*d->block_size);
}

#else

char *guppi_databuf_header(struct guppi_databuf *d, int block_id) {
    return((char *)d + d->struct_size + block_id*d->header_size);
}

char *guppi_databuf_index(struct guppi_databuf *d, int block_id) {
    return((char *)d + d->struct_size + d->n_block*d->header_size
            + block_id*d->index_size);
}

char *guppi_databuf_data(struct guppi_databuf *d, int block_id) {
    return((char *)d + d->struct_size + d->n_block*d->header_size
            + d->n_block*d->index_size + block_id*d->block_size);
}
#endif

struct paper_input_databuf *paper_databuf_attach(int databuf_id) {

    /* Get shmid */
    int shmid;
    shmid = shmget(GUPPI_DATABUF_KEY + databuf_id - 1, 0, 0666);
    if (shmid==-1) {
        // Doesn't exist, exit quietly otherwise complain
        if (errno!=ENOENT)
            guppi_error("guppi_databuf_attach", "shmget error");
        return(NULL);
    }

    /* Attach */
    struct guppi_databuf *d;
    d = shmat(shmid, NULL, 0);
    if (d==(void *)-1) {
        guppi_error("guppi_databuf_attach", "shmat error");
        return(NULL);
    }

    return(d);

}

int guppi_databuf_block_status(struct guppi_databuf *d, int block_id) {
    return(semctl(d->semid, block_id, GETVAL));
}

int guppi_databuf_total_status(struct guppi_databuf *d) {

    /* Get all values at once */
    union semun arg;
    arg.array = (unsigned short *)malloc(sizeof(unsigned short)*d->n_block);
    memset(arg.array, 0, sizeof(unsigned short)*d->n_block);
    semctl(d->semid, 0, GETALL, arg);
    int i,tot=0;
    for (i=0; i<d->n_block; i++) tot+=arg.array[i];
    free(arg.array);
    return(tot);

}

int guppi_databuf_wait_free(struct guppi_databuf *d, int block_id) {
    int rv;
    struct sembuf op;
    op.sem_num = block_id;
    op.sem_op = 0;
    op.sem_flg = 0;
    struct timespec timeout;
    timeout.tv_sec = 0;
    timeout.tv_nsec = 250000000;
    rv = semtimedop(d->semid, &op, 1, &timeout);
    if (rv==-1) { 
        if (errno==EAGAIN) return(GUPPI_TIMEOUT);
        if (errno==EINTR) return(GUPPI_ERR_SYS);
        guppi_error("guppi_databuf_wait_free", "semop error");
        perror("semop");
        return(GUPPI_ERR_SYS);
    }
    return(0);
}

int guppi_databuf_wait_filled(struct guppi_databuf *d, int block_id) {
    /* This needs to wait for the semval of the given block
     * to become > 0, but NOT immediately decrement it to 0.
     * Probably do this by giving an array of semops, since
     * (afaik) the whole array happens atomically:
     * step 1: wait for val=1 then decrement (semop=-1)
     * step 2: increment by 1 (semop=1)
     */
    int rv;
    struct sembuf op[2];
    op[0].sem_num = op[1].sem_num = block_id;
    op[0].sem_flg = op[1].sem_flg = 0;
    op[0].sem_op = -1;
    op[1].sem_op = 1;
    struct timespec timeout;
    timeout.tv_sec = 0;
    timeout.tv_nsec = 250000000;
    rv = semtimedop(d->semid, op, 2, &timeout);
    if (rv==-1) { 
        if (errno==EAGAIN) return(GUPPI_TIMEOUT);
        // Don't complain on a signal interruption
        if (errno==EINTR) return(GUPPI_ERR_SYS);
        guppi_error("guppi_databuf_wait_filled", "semop error");
        perror("semop");
        return(GUPPI_ERR_SYS);
    }
    return(0);
}

int guppi_databuf_set_free(struct guppi_databuf *d, int block_id) {
    /* This function should always succeed regardless of the current
     * state of the specified databuf.  So we use semctl (not semop) to set
     * the value to zero.
     */
    int rv;
    union semun arg;
    arg.val = 0;
    rv = semctl(d->semid, block_id, SETVAL, arg);
    if (rv==-1) { 
        guppi_error("guppi_databuf_set_free", "semctl error");
        return(GUPPI_ERR_SYS);
    }
    return(0);
}

int guppi_databuf_set_filled(struct guppi_databuf *d, int block_id) {
    /* This function should always succeed regardless of the current
     * state of the specified databuf.  So we use semctl (not semop) to set
     * the value to one.
     */
    int rv;
    union semun arg;
    arg.val = 1;
    rv = semctl(d->semid, block_id, SETVAL, arg);
    if (rv==-1) { 
        guppi_error("guppi_databuf_set_filled", "semctl error");
        return(GUPPI_ERR_SYS);
    }
    return(0);
}