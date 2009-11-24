#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <qthread/qutil.h>
#include <qthread/qthread.h>
#include <qthread/futurelib.h>
#include <qthread_asserts.h>           /* for assert() toggling */
#include <qthread/cacheline.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef MT_LOOP_CHUNK
#define MT_LOOP_CHUNK 10000
#endif

#define STRUCT(_structname_, _rtype_) struct _structname_ \
{ \
    const _rtype_ *array; \
    _rtype_ Q_ALIGNED(8) ret; \
    size_t start, stop; \
    const _rtype_ *addlast; \
    struct _structname_ *backptr; \
}
#define INNER_LOOP(_fname_,_structtype_,_opmacro_) static aligned_t _fname_(qthread_t *me, struct _structtype_ *args) \
{ \
    size_t i; \
    args->ret = args->array[args->start]; \
    for (i = args->start + 1; i < args->stop; i++) { \
	_opmacro_(args->ret, args->array[i]); \
    } \
    if (args->addlast) { \
	qthread_readFF(me, NULL, (aligned_t*)args->addlast); \
	_opmacro_(args->ret, *(args->addlast)); \
	free(args->backptr); \
    } \
    qthread_fill(me, (aligned_t*)&(args->ret)); \
    return 0; \
}
#define INNER_LOOP_FF(_fname_,_structtype_,_opmacro_) static aligned_t _fname_(qthread_t *me, struct _structtype_ *args) \
{ \
    size_t i; \
    qthread_readFF(me, NULL, (aligned_t*)(args->array + args->start)); \
    args->ret = args->array[args->start]; \
    for (i = args->start + 1; i < args->stop; i++) { \
	qthread_readFF(me, NULL, (aligned_t*)(args->array + i)); \
	_opmacro_(args->ret, args->array[i]); \
    } \
    if (args->addlast) { \
	qthread_readFF(me, NULL, (aligned_t*)args->addlast); \
	_opmacro_(args->ret, *(args->addlast)); \
	free(args->backptr); \
    } \
    qthread_fill(me, (aligned_t*)&(args->ret)); \
    return 0; \
}
#define OUTER_LOOP(_fname_,_structtype_,_opmacro_,_rtype_,_innerfunc_,_innerfuncff_) \
_rtype_ _fname_(qthread_t *me, const _rtype_ *array, size_t length, int checkfeb) \
{ \
    size_t i, start = 0; \
    _rtype_ *waitfor = NULL, myret; \
    struct _structtype_ *bkptr = NULL; \
    /* abort if checkfeb == 1 && aligned_t is too big */ \
    assert(checkfeb == 0 || sizeof(aligned_t) <= sizeof(_rtype_)); \
    while (start + MT_LOOP_CHUNK < length) { \
	/* spawn off an MT_LOOP_CHUNK-sized segment of the first part of the array */ \
	struct _structtype_ *left_args = \
	    malloc(sizeof(struct _structtype_)); \
	\
	left_args->array = array; \
	left_args->start = start; \
	left_args->stop = start + MT_LOOP_CHUNK; \
	start += MT_LOOP_CHUNK; \
	left_args->backptr = bkptr; \
	bkptr = left_args; \
	left_args->addlast = waitfor; \
	waitfor = &(left_args->ret); \
	qthread_empty(me, (aligned_t*)&(left_args->ret)); \
	if (checkfeb) { \
	    future_fork((qthread_f) _innerfuncff_, left_args, NULL); \
	} else { \
	    future_fork((qthread_f) _innerfunc_, left_args, NULL); \
	} \
    } \
    if (checkfeb) { \
	qthread_readFF(me, NULL, (aligned_t*)(array + start)); \
	myret = array[start]; \
	for (i = start + 1; i < length; i++) { \
	    qthread_readFF(me, NULL, (aligned_t*)(array + i)); \
	    _opmacro_(myret, array[i]); \
	} \
    } else { \
	myret = array[start]; \
	for (i = start + 1; i < length; i++) { \
	    _opmacro_(myret, array[i]); \
	} \
    } \
    if (waitfor) { \
	qthread_readFF(me, NULL, (aligned_t*)waitfor); \
	_opmacro_(myret, *waitfor); \
	free(bkptr); \
    } \
    return myret; \
}

#define SUM_MACRO(sum,add) sum += (add)
#define MULT_MACRO(prod,factor) prod *= (factor)
#define MAX_MACRO(max, contender) if (max < (contender)) max = (contender)
#define MIN_MACRO(max, contender) if (max > (contender)) max = (contender)

/* These are the functions for computing things about doubles */
STRUCT(qutil_ds_args, double);
INNER_LOOP(qutil_double_sum_inner, qutil_ds_args, SUM_MACRO)
INNER_LOOP_FF(qutil_double_FF_sum_inner, qutil_ds_args, SUM_MACRO)
OUTER_LOOP(qutil_double_sum, qutil_ds_args, SUM_MACRO, double,
	qutil_double_sum_inner, qutil_double_FF_sum_inner)
INNER_LOOP(qutil_double_mult_inner, qutil_ds_args, MULT_MACRO)
INNER_LOOP_FF(qutil_double_FF_mult_inner, qutil_ds_args, MULT_MACRO)
OUTER_LOOP(qutil_double_mult, qutil_ds_args, MULT_MACRO, double,
	   qutil_double_mult_inner, qutil_double_FF_mult_inner)
INNER_LOOP(qutil_double_max_inner, qutil_ds_args, MAX_MACRO)
INNER_LOOP_FF(qutil_double_FF_max_inner, qutil_ds_args, MAX_MACRO)
OUTER_LOOP(qutil_double_max, qutil_ds_args, MAX_MACRO, double,
	qutil_double_max_inner, qutil_double_FF_max_inner)
INNER_LOOP(qutil_double_min_inner, qutil_ds_args, MIN_MACRO)
INNER_LOOP_FF(qutil_double_FF_min_inner, qutil_ds_args, MIN_MACRO)
OUTER_LOOP(qutil_double_min, qutil_ds_args, MIN_MACRO, double,
	qutil_double_min_inner, qutil_double_FF_min_inner)
/* These are the functions for computing things about unsigned ints */
STRUCT(qutil_uis_args, aligned_t);
INNER_LOOP(qutil_uint_sum_inner, qutil_uis_args, SUM_MACRO)
INNER_LOOP_FF(qutil_uint_FF_sum_inner, qutil_uis_args, SUM_MACRO)
OUTER_LOOP(qutil_uint_sum, qutil_uis_args, SUM_MACRO, aligned_t,
	qutil_uint_sum_inner, qutil_uint_FF_sum_inner)
INNER_LOOP(qutil_uint_mult_inner, qutil_uis_args, MULT_MACRO)
INNER_LOOP_FF(qutil_uint_FF_mult_inner, qutil_uis_args, MULT_MACRO)
OUTER_LOOP(qutil_uint_mult, qutil_uis_args, MULT_MACRO, aligned_t,
	qutil_uint_mult_inner, qutil_uint_FF_mult_inner)
INNER_LOOP(qutil_uint_max_inner, qutil_uis_args, MAX_MACRO)
INNER_LOOP_FF(qutil_uint_FF_max_inner, qutil_uis_args, MAX_MACRO)
OUTER_LOOP(qutil_uint_max, qutil_uis_args, MAX_MACRO, aligned_t,
	qutil_uint_max_inner, qutil_uint_FF_max_inner)
INNER_LOOP(qutil_uint_min_inner, qutil_uis_args, MIN_MACRO)
INNER_LOOP_FF(qutil_uint_FF_min_inner, qutil_uis_args, MIN_MACRO)
OUTER_LOOP(qutil_uint_min, qutil_uis_args, MIN_MACRO, aligned_t,
	qutil_uint_min_inner, qutil_uint_FF_min_inner)
/* These are the functions for computing things about signed ints */
STRUCT(qutil_is_args, saligned_t);
INNER_LOOP(qutil_int_sum_inner, qutil_is_args, SUM_MACRO)
INNER_LOOP_FF(qutil_int_FF_sum_inner, qutil_is_args, SUM_MACRO)
OUTER_LOOP(qutil_int_sum, qutil_is_args, SUM_MACRO, saligned_t, qutil_int_sum_inner,
	qutil_int_FF_sum_inner)
INNER_LOOP(qutil_int_mult_inner, qutil_is_args, MULT_MACRO)
INNER_LOOP_FF(qutil_int_FF_mult_inner, qutil_is_args, MULT_MACRO)
OUTER_LOOP(qutil_int_mult, qutil_is_args, MULT_MACRO, saligned_t,
	qutil_int_mult_inner, qutil_int_FF_mult_inner)
INNER_LOOP(qutil_int_max_inner, qutil_is_args, MAX_MACRO)
INNER_LOOP_FF(qutil_int_FF_max_inner, qutil_is_args, MAX_MACRO)
OUTER_LOOP(qutil_int_max, qutil_is_args, MAX_MACRO, saligned_t, qutil_int_max_inner,
	qutil_int_FF_max_inner)
INNER_LOOP(qutil_int_min_inner, qutil_is_args, MIN_MACRO)
INNER_LOOP_FF(qutil_int_FF_min_inner, qutil_is_args, MIN_MACRO)
OUTER_LOOP(qutil_int_min, qutil_is_args, MIN_MACRO, saligned_t, qutil_int_min_inner,
	qutil_int_FF_min_inner)

typedef int (*cmp_f) (const void *a, const void *b);

// This is based on a public-domain non-recursive C qsort by Darel Rex Finley
// Obtained from http://alienryderflex.com/quicksort/
// This sort CAN fail if there are more than 2^MAX elements (or so)
static void drf_qsort_any(void *const array, const size_t elements,
			  const size_t elem_size, const cmp_f cmp)
{
    const size_t MAX = log(elements) + 5;
    ssize_t beg[MAX], end[MAX], i = 0, L, R, swap;
    char piv[elem_size];
    char *const arr = (char *const)array;

    beg[0] = 0;
    end[0] = elements;
    while (i >= 0) {
	assert(i < MAX);
	L = beg[i];
	R = end[i] - 1;
	if (L < R) {
	    memcpy(piv, arr + (L * elem_size), elem_size);
	    while (L < R) {
		while (cmp(arr + (R * elem_size), &piv) >= 0 && L < R)
		    R--;
		if (L < R)
		    memcpy(arr + (L++ * elem_size), arr + (R * elem_size),
			   elem_size);
		while (cmp(arr + (L * elem_size), &piv) <= 0 && L < R)
		    L++;
		if (L < R)
		    memcpy(arr + (R-- * elem_size), arr + (L * elem_size),
			   elem_size);
	    }
	    memcpy(arr + (L * elem_size), &piv, elem_size);
	    beg[i + 1] = L + 1;
	    end[i + 1] = end[i];
	    end[i++] = L;
	    if (end[i] - beg[i] > end[i - 1] - beg[i - 1]) {
		swap = beg[i];
		beg[i] = beg[i - 1];
		beg[i - 1] = swap;
		swap = end[i];
		end[i] = end[i - 1];
		end[i - 1] = swap;
	    }
	} else {
	    i--;
	}
    }
}

static void drf_qsort8(void *const array, const size_t elements,
		       const cmp_f cmp)
{
    const size_t MAX = log(elements) + 5;
    ssize_t beg[MAX], end[MAX], i = 0, L, R, swap;
    uint64_t piv;
    uint64_t *const arr = (uint64_t * const)array;

    beg[0] = 0;
    end[0] = elements;
    while (i >= 0) {
	assert(i < MAX);
	L = beg[i];
	R = end[i] - 1;
	if (L < R) {
	    piv = arr[L];
	    while (L < R) {
		while (cmp(&arr[R], &piv) >= 0 && L < R)
		    R--;
		if (L < R)
		    arr[L++] = arr[R];
		while (cmp(&arr[L], &piv) <= 0 && L < R)
		    L++;
		if (L < R)
		    arr[R--] = arr[L];
	    }
	    arr[L] = piv;
	    beg[i + 1] = L + 1;
	    end[i + 1] = end[i];
	    end[i++] = L;
	    if (end[i] - beg[i] > end[i - 1] - beg[i - 1]) {
		swap = beg[i];
		beg[i] = beg[i - 1];
		beg[i - 1] = swap;
		swap = end[i];
		end[i] = end[i - 1];
		end[i - 1] = swap;
	    }
	} else {
	    i--;
	}
    }
}

static void drf_qsort4(void *const array, const size_t elements,
		       const cmp_f cmp)
{
    const size_t MAX = log(elements) + 5;
    ssize_t beg[MAX], end[MAX], i = 0, L, R, swap;
    uint32_t piv;
    uint32_t *const arr = (uint32_t * const)array;

    beg[0] = 0;
    end[0] = elements;
    while (i >= 0) {
	assert(i < MAX);
	L = beg[i];
	R = end[i] - 1;
	if (L < R) {
	    piv = arr[L];
	    while (L < R) {
		while (cmp(&arr[R], &piv) >= 0 && L < R)
		    R--;
		if (L < R)
		    arr[L++] = arr[R];
		while (cmp(&arr[L], &piv) <= 0 && L < R)
		    L++;
		if (L < R)
		    arr[R--] = arr[L];
	    }
	    arr[L] = piv;
	    beg[i + 1] = L + 1;
	    end[i + 1] = end[i];
	    end[i++] = L;
	    if (end[i] - beg[i] > end[i - 1] - beg[i - 1]) {
		swap = beg[i];
		beg[i] = beg[i - 1];
		beg[i - 1] = swap;
		swap = end[i];
		end[i] = end[i - 1];
		end[i - 1] = swap;
	    }
	} else {
	    i--;
	}
    }
}

static void drf_qsort(void *const array, const size_t elements,
		      const size_t elem_size, const cmp_f cmp)
{
    switch (elem_size) {
	case 4:
	    drf_qsort4(array, elements, cmp);
	    break;
	case 8:
	    drf_qsort8(array, elements, cmp);
	    break;
	default:
	    drf_qsort_any(array, elements, elem_size, cmp);
	    break;
    }
}

static void drf_qsort_dbl(double *const arr, const size_t elements)
{
    const size_t MAX = log(elements) + 5;
    ssize_t beg[MAX], end[MAX], i = 0, L, R, swap;
    double piv;

    beg[0] = 0;
    end[0] = elements;
    while (i >= 0) {
	assert(i < MAX);
	L = beg[i];
	R = end[i] - 1;
	if (L < R) {
	    piv = arr[L];
	    while (L < R) {
		while (arr[R] >= piv && L < R)
		    R--;
		if (L < R)
		    arr[L++] = arr[R];
		while (arr[L] <= piv && L < R)
		    L++;
		if (L < R)
		    arr[R--] = arr[L];
	    }
	    arr[L] = piv;
	    beg[i + 1] = L + 1;
	    end[i + 1] = end[i];
	    end[i++] = L;
	    if (end[i] - beg[i] > end[i - 1] - beg[i - 1]) {
		swap = beg[i];
		beg[i] = beg[i - 1];
		beg[i - 1] = swap;
		swap = end[i];
		end[i] = end[i - 1];
		end[i - 1] = swap;
	    }
	} else {
	    i--;
	}
    }
}

static void drf_qsort_algt(aligned_t * const arr, const size_t elements)
{
    const size_t MAX = log(elements) + 5;
    ssize_t beg[MAX], end[MAX], i = 0, L, R, swap;
    aligned_t piv;

    beg[0] = 0;
    end[0] = elements;
    while (i >= 0) {
	assert(i < MAX);
	L = beg[i];
	R = end[i] - 1;
	if (L < R) {
	    piv = arr[L];
	    while (L < R) {
		while (arr[R] >= piv && L < R)
		    R--;
		if (L < R)
		    arr[L++] = arr[R];
		while (arr[L] <= piv && L < R)
		    L++;
		if (L < R)
		    arr[R--] = arr[L];
	    }
	    arr[L] = piv;
	    beg[i + 1] = L + 1;
	    end[i + 1] = end[i];
	    end[i++] = L;
	    if (end[i] - beg[i] > end[i - 1] - beg[i - 1]) {
		swap = beg[i];
		beg[i] = beg[i - 1];
		beg[i - 1] = swap;
		swap = end[i];
		end[i] = end[i - 1];
		end[i - 1] = swap;
	    }
	} else {
	    i--;
	}
    }
}

struct qutil_mergesort_args {
    double *array;
    size_t first_start, first_stop;
    size_t second_start, second_stop;
};

static aligned_t qutil_mergesort_presort(qthread_t * Q_UNUSED me,
				  struct qutil_mergesort_args * args)
{
    drf_qsort_dbl(args->array + args->first_start,
	  args->first_stop - args->first_start + 1);
    return 0;
}

static aligned_t qutil_mergesort_inner(qthread_t * Q_UNUSED me,
				struct qutil_mergesort_args * args)
{
    double *array = args->array;

    while ((args->first_start <= args->first_stop) &&
	   (args->second_start <= args->second_stop)) {
	if (array[args->first_start] < array[args->second_start]) {
	    args->first_start++;
	} else {
	    /* XXX: I want a faster in-place merge!!!! */
	    /* The next element comes from the second list, move the
	     * array[second_start] element into the next position and move
	     * all the other elements up */
	    double temp = array[args->second_start];
	    size_t k;

	    for (k = args->second_start - 1; k >= args->first_start; k--) {
		array[k + 1] = array[k];
		if (k == 0)
		    break;
	    }
	    array[args->first_start] = temp;
	    args->first_start++;
	    args->first_stop++;
	    args->second_start++;
	}
    }
    return 0;
}

void qutil_mergesort(qthread_t * me, double *array, size_t length)
{
    /* first, decide how much of the array each thread gets */
    size_t chunksize = MT_LOOP_CHUNK;

    /* first, decide how many threads to use... */
    size_t numthreads;
    aligned_t *rets;
    size_t i;
    struct qutil_mergesort_args *args;

    chunksize = 10;
    /* first, an initial qsort() */
    numthreads = length / chunksize;
    if (length - (numthreads * chunksize))
	numthreads++;
    rets = malloc(sizeof(aligned_t) * numthreads);
    args = malloc(sizeof(struct qutil_mergesort_args) * numthreads);
    for (i = 0; i < numthreads; i++) {
	args[i].array = array;
	args[i].first_start = i * chunksize;
	args[i].first_stop = (i + 1) * chunksize - 1;
	if (args[i].first_stop >= length)
	    args[i].first_stop = length - 1;

	future_fork((qthread_f) qutil_mergesort_presort, args + i, rets + i);
    }
    for (i = 0; i < numthreads; i++) {
	qthread_readFF(me, NULL, rets + i);
    }
    free(rets);
    free(args);
    /* prepare scratch memory */
    if (chunksize <= length) {
	numthreads = (length - chunksize) / (2 * chunksize);
	if ((length - chunksize) - (2 * chunksize * numthreads))
	    numthreads++;
	rets = malloc(sizeof(aligned_t) * numthreads);
	assert(rets);
	args = malloc(sizeof(struct qutil_mergesort_args) * numthreads);
	assert(args);
	numthreads = 0;
    }
    /* now, commence with the merging */
    while (chunksize <= length) {
	i = 0;
	numthreads = 0;
	while (i < length - chunksize) {
	    args[numthreads].array = array;
	    args[numthreads].first_start = i;
	    args[numthreads].first_stop = i + chunksize - 1;
	    args[numthreads].second_start = i + chunksize;
	    args[numthreads].second_stop =
		((i + 2 * chunksize - 1) <
		 (length - 1)) ? (i + 2 * chunksize - 1) : (length - 1);
	    future_fork((qthread_f) qutil_mergesort_inner, args + numthreads,
			rets + numthreads);
	    i += 2 * chunksize;
	    numthreads++;
	}
	for (i = 0; i < numthreads; i++) {
	    qthread_readFF(me, NULL, rets + i);
	}
	chunksize *= 2;
    }
    if (rets) {
	free(rets);
	free(args);
    }
}

#define SWAP(t, a, m, n) do { register t temp=a[m]; a[m]=a[n]; a[n]=temp; } while (0)
#define MT_CHUNKSIZE (qthread_cacheline()/sizeof(double))

struct qutil_qsort_args {
    double *array;
    double pivot;
    size_t length, jump, offset;
    aligned_t *furthest_leftwall, *furthest_rightwall;
};

static inline aligned_t qutil_qsort_partition(qthread_t * me, struct qutil_qsort_args *args)
{
    double *a = args->array;
    const double pivot = args->pivot;
    const size_t length = args->length;
    const size_t jump = args->jump;
    size_t leftwall, rightwall;

    leftwall = 0;
    rightwall = length - 1;
    /* adjust the edges; this is critical for this algorithm */
    while (a[leftwall] <= pivot) {
	if ((leftwall + 1) % MT_CHUNKSIZE != 0) {
	    leftwall++;
	} else {
	    leftwall += jump;
	}
	if (rightwall < leftwall)
	    goto quickexit;
    }
    while (a[rightwall] > pivot) {
	if (rightwall % MT_CHUNKSIZE != 0) {
	    if (rightwall == 0)
		goto quickexit;
	    rightwall--;
	} else {
	    if (rightwall < jump)
		goto quickexit;
	    rightwall -= jump;
	}
	if (rightwall < leftwall)
	    goto quickexit;
    }
    SWAP(double, a, leftwall, rightwall);
    while (1) {
	do {
	    leftwall += ((leftwall + 1) % MT_CHUNKSIZE != 0) ? 1 : jump;
	    if (rightwall < leftwall)
		goto quickexit;
	} while (a[leftwall] <= pivot);
	if (rightwall <= leftwall)
	    break;
	do {
	    if (rightwall % MT_CHUNKSIZE != 0) {
		if (rightwall == 0)
		    goto quickexit;
		rightwall--;
	    } else {
		if (rightwall < jump)
		    goto quickexit;
		rightwall -= jump;
	    }
	} while (a[rightwall] > pivot);
	if (rightwall <= leftwall)
	    break;
	SWAP(double, a, leftwall, rightwall);
    }
quickexit:
    qthread_lock(me, args->furthest_leftwall);
    if (leftwall + args->offset < *args->furthest_leftwall) {
	*args->furthest_leftwall = leftwall + args->offset;
    }
    if (rightwall + args->offset > *args->furthest_rightwall) {
	*args->furthest_rightwall = rightwall + args->offset;
    }
    qthread_unlock(me, args->furthest_leftwall);
    return 0;
}

struct qutil_qsort_iargs {
    double *array;
    size_t length;
};

struct qutil_qsort_iprets {
    aligned_t leftwall, rightwall;
};

static inline struct qutil_qsort_iprets qutil_qsort_inner_partitioner(qthread_t * me,
							double *array,
							const size_t length,
							const double pivot)
{
    /* choose the number of threads to use */
    const size_t numthreads = length / MT_LOOP_CHUNK + ((length % MT_LOOP_CHUNK) ? 1 : 0);
    /* calculate the megachunk information for determining the array lengths
     * each thread will be fed. */
    const size_t megachunk_size = MT_CHUNKSIZE * numthreads;
    /* just used as a boolean test */
    const size_t extra_chunks = length % megachunk_size;

    size_t megachunks = length / (MT_CHUNKSIZE * numthreads);
    struct qutil_qsort_iprets retval = { ((aligned_t) - 1), 0 };
    aligned_t *rets;
    struct qutil_qsort_args *args;
    size_t i;

    rets = malloc(sizeof(aligned_t) * numthreads);
    args = malloc(sizeof(struct qutil_qsort_args) * numthreads);
    /* spawn threads to do the partitioning */
    for (i = 0; i < numthreads; i++) {
	args[i].array = array + (i * MT_CHUNKSIZE);
	args[i].offset = i * MT_CHUNKSIZE;
	args[i].pivot = pivot;
	args[i].jump = (numthreads - 1) * MT_CHUNKSIZE + 1;
	args[i].furthest_leftwall = &retval.leftwall;
	args[i].furthest_rightwall = &retval.rightwall;
	if (extra_chunks != 0) {
	    args[i].length = megachunks * (megachunk_size) + MT_CHUNKSIZE;
	    if (args[i].length + args[i].offset >= length) {
		args[i].length = length - args[i].offset;
		megachunks--;
	    }
	} else {
	    args[i].length = length - megachunk_size + MT_CHUNKSIZE;
	}
	/* qutil_qsort_partition(me, args+i); */
	/* future_fork((qthread_f)qutil_qsort_partition, args+i, rets+i); */
	qthread_fork((qthread_f) qutil_qsort_partition, args + i, rets + i);
    }
    for (i = 0; i < numthreads; i++) {
	qthread_readFF(me, NULL, rets + i);
    }
    free(args);
    free(rets);

    return retval;
}

static inline aligned_t qutil_qsort_inner(qthread_t * me, struct qutil_qsort_iargs * a)
{
    double *array = a->array, pivot;
    struct qutil_qsort_iprets furthest;

    /* choose the number of threads to use */
    if (a->length <= MT_LOOP_CHUNK) {  /* shortcut */
	drf_qsort_dbl(array, a->length);
	return 0;
    }
    furthest.leftwall = 0;
    furthest.rightwall = a->length - 1;
    /* tri-median pivot selection */
    {
	size_t i;
	i = a->length / 2;
	if (array[0] > array[i]) {
	    SWAP(double, array, 0, i);
	}
	if (array[0] > array[a->length - 1]) {
	    SWAP(double, array, 0, a->length - 1);
	}
	if (array[i] > array[a->length - 1]) {
	    SWAP(double, array, i, a->length - 1);
	}
	pivot = array[i];
    }
    while (furthest.rightwall > furthest.leftwall &&
	   furthest.rightwall - furthest.leftwall > (2 * MT_LOOP_CHUNK)) {
	size_t offset = furthest.leftwall;

	furthest =
	    qutil_qsort_inner_partitioner(me, a->array + furthest.leftwall,
					  furthest.rightwall -
					  furthest.leftwall + 1, pivot);
	furthest.leftwall += offset;
	furthest.rightwall += offset;
    }
    /* data between furthest.leftwall and furthest.rightwall is unlikely to
     * be partitioned correctly */
    {
	size_t leftwall = furthest.leftwall, rightwall = furthest.rightwall;

	while (leftwall < rightwall && array[leftwall] <= pivot)
	    leftwall++;
	while (leftwall < rightwall && array[rightwall] > pivot)
	    rightwall--;
	if (leftwall < rightwall) {
	    SWAP(double, array, leftwall, rightwall);
	    while (1) {
		while (++leftwall < rightwall && array[leftwall] <= pivot) ;
		if (rightwall < leftwall)
		    break;
		while (leftwall < --rightwall && array[rightwall] > pivot) ;
		if (rightwall < leftwall)
		    break;
		SWAP(double, array, leftwall, rightwall);
	    }
	}
	if (array[rightwall] <= pivot) {
	    rightwall++;
	}
	/* now, spawn the next two iterations */
	{
	    struct qutil_qsort_iargs na[2];
	    aligned_t rets[2] = { 1, 1 };

	    na[0].array = array;
	    na[0].length = rightwall;
	    na[1].array = array + rightwall;
	    na[1].length = a->length - rightwall;
	    if (na[0].length > 0) {
		rets[0] = 0;
		/* future_fork((qthread_f)qutil_qsort_inner, na, rets); */
		/* qutil_qsort_inner(me, na); */
		qthread_fork((qthread_f) qutil_qsort_inner, na, rets);
	    }
	    if (na[1].length > 0 && a->length > rightwall) {
		rets[1] = 0;
		/* future_fork((qthread_f)qutil_qsort_inner, na+1, rets+1); */
		/* qutil_qsort_inner(me, na+1); */
		qthread_fork((qthread_f) qutil_qsort_inner, na + 1, rets + 1);
	    }
	    if (rets[0] == 0) {
		qthread_readFF(me, NULL, rets);
	    }
	    if (rets[1] == 0) {
		qthread_readFF(me, NULL, rets + 1);
	    }
	}
    }
    return 0;
}

void qutil_qsort(qthread_t * me, double *array, const size_t length)
{
    struct qutil_qsort_iargs arg;

    arg.array = array;
    arg.length = length;

    qutil_qsort_inner(me, &arg);
}

struct qutil_aligned_qsort_args {
    aligned_t *array;
    aligned_t pivot;
    size_t length, jump, offset;
    aligned_t *furthest_leftwall, *furthest_rightwall;
};

static inline aligned_t qutil_aligned_qsort_partition(qthread_t * me, struct qutil_aligned_qsort_args *args)
{
    aligned_t *a = args->array;
    const aligned_t pivot = args->pivot;
    const size_t length = args->length;
    const size_t jump = args->jump;
    size_t leftwall, rightwall;

    leftwall = 0;
    rightwall = length - 1;
    /* adjust the edges; this is critical for this algorithm */
    while (a[leftwall] <= pivot) {
	if ((leftwall + 1) % MT_CHUNKSIZE != 0) {
	    leftwall++;
	} else {
	    leftwall += jump;
	}
	if (rightwall < leftwall)
	    goto quickexit;
    }
    while (a[rightwall] > pivot) {
	if (rightwall % MT_CHUNKSIZE != 0) {
	    if (rightwall == 0)
		goto quickexit;
	    rightwall--;
	} else {
	    if (rightwall < jump)
		goto quickexit;
	    rightwall -= jump;
	}
	if (rightwall < leftwall)
	    goto quickexit;
    }
    SWAP(aligned_t, a, leftwall, rightwall);
    while (1) {
	do {
	    leftwall += ((leftwall + 1) % MT_CHUNKSIZE != 0) ? 1 : jump;
	    if (rightwall < leftwall)
		goto quickexit;
	} while (a[leftwall] <= pivot);
	if (rightwall <= leftwall)
	    break;
	do {
	    if (rightwall % MT_CHUNKSIZE != 0) {
		if (rightwall == 0)
		    goto quickexit;
		rightwall--;
	    } else {
		if (rightwall < jump)
		    goto quickexit;
		rightwall -= jump;
	    }
	} while (a[rightwall] > pivot);
	if (rightwall <= leftwall)
	    break;
	SWAP(aligned_t, a, leftwall, rightwall);
    }
quickexit:
    qthread_lock(me, args->furthest_leftwall);
    if (leftwall + args->offset < *args->furthest_leftwall) {
	*args->furthest_leftwall = leftwall + args->offset;
    }
    if (rightwall + args->offset > *args->furthest_rightwall) {
	*args->furthest_rightwall = rightwall + args->offset;
    }
    qthread_unlock(me, args->furthest_leftwall);
    return 0;
}


static int alcmp(const void *a, const void *b)
{
    if ((*(aligned_t *)a) < (*(aligned_t *)b))
	return -1;
    if ((*(aligned_t *)a) > (*(aligned_t *)b))
	return 1;
    return 0;
}

struct qutil_aligned_qsort_iargs {
    aligned_t *array;
    size_t length;
};

static inline struct qutil_qsort_iprets qutil_aligned_qsort_inner_partitioner(qthread_t * me,
							aligned_t *array,
							const size_t length,
							const aligned_t pivot)
{
    /* choose the number of threads to use */
    const size_t numthreads = length / MT_LOOP_CHUNK + ((length % MT_LOOP_CHUNK) ? 1 : 0);
    /* calculate the megachunk information for determining the array lengths
     * each thread will be fed. */
    const size_t megachunk_size = MT_CHUNKSIZE * numthreads;
    /* just used as a boolean test */
    const size_t extra_chunks = length % megachunk_size;

    size_t megachunks = length / (MT_CHUNKSIZE * numthreads);
    struct qutil_qsort_iprets retval = { ((aligned_t) - 1), 0 };
    aligned_t *rets;
    struct qutil_aligned_qsort_args *args;
    size_t i;

    rets = malloc(sizeof(aligned_t) * numthreads);
    args = malloc(sizeof(struct qutil_aligned_qsort_args) * numthreads);
    /* spawn threads to do the partitioning */
    for (i = 0; i < numthreads; i++) {
	args[i].array = array + (i * MT_CHUNKSIZE);
	args[i].offset = i * MT_CHUNKSIZE;
	args[i].pivot = pivot;
	args[i].jump = (numthreads - 1) * MT_CHUNKSIZE + 1;
	args[i].furthest_leftwall = &retval.leftwall;
	args[i].furthest_rightwall = &retval.rightwall;
	if (extra_chunks != 0) {
	    args[i].length = megachunks * (megachunk_size) + MT_CHUNKSIZE;
	    if (args[i].length + args[i].offset >= length) {
		args[i].length = length - args[i].offset;
		megachunks--;
	    }
	} else {
	    args[i].length = length - megachunk_size + MT_CHUNKSIZE;
	}
	/* qutil_aligned_qsort_partition(me, args+i); */
	/* future_fork((qthread_f)qutil_aligned_qsort_partition, args+i, rets+i); */
	qthread_fork((qthread_f) qutil_aligned_qsort_partition, args + i, rets + i);
    }
    for (i = 0; i < numthreads; i++) {
	qthread_readFF(me, NULL, rets + i);
    }
    free(args);
    free(rets);

    return retval;
}

static inline aligned_t qutil_aligned_qsort_inner(qthread_t * me, struct qutil_aligned_qsort_iargs * a)
{
    aligned_t *array = a->array, pivot;
    struct qutil_qsort_iprets furthest;

    /* choose the number of threads to use */
    if (a->length <= MT_LOOP_CHUNK) {  /* shortcut */
	drf_qsort_algt(array, a->length);
	return 0;
    }
    furthest.leftwall = 0;
    furthest.rightwall = a->length - 1;
    /* tri-median pivot selection */
    {
	size_t i;
	i = a->length / 2;
	if (array[0] > array[i]) {
	    SWAP(aligned_t, array, 0, i);
	}
	if (array[0] > array[a->length - 1]) {
	    SWAP(aligned_t, array, 0, a->length - 1);
	}
	if (array[i] > array[a->length - 1]) {
	    SWAP(aligned_t, array, i, a->length - 1);
	}
	pivot = array[i];
    }
    while (furthest.rightwall > furthest.leftwall &&
	   furthest.rightwall - furthest.leftwall > (2 * MT_LOOP_CHUNK)) {
	size_t offset = furthest.leftwall;

	furthest =
	    qutil_aligned_qsort_inner_partitioner(me, a->array + furthest.leftwall,
					  furthest.rightwall -
					  furthest.leftwall + 1, pivot);
	furthest.leftwall += offset;
	furthest.rightwall += offset;
    }
    /* data between furthest.leftwall and furthest.rightwall is unlikely to
     * be partitioned correctly */
    {
	size_t leftwall = furthest.leftwall, rightwall = furthest.rightwall;

	while (leftwall < rightwall && array[leftwall] <= pivot)
	    leftwall++;
	while (leftwall < rightwall && array[rightwall] > pivot)
	    rightwall--;
	if (leftwall < rightwall) {
	    SWAP(aligned_t, array, leftwall, rightwall);
	    while (1) {
		while (++leftwall < rightwall && array[leftwall] <= pivot) ;
		if (rightwall < leftwall)
		    break;
		while (leftwall < --rightwall && array[rightwall] > pivot) ;
		if (rightwall < leftwall)
		    break;
		SWAP(aligned_t, array, leftwall, rightwall);
	    }
	}
	if (array[rightwall] <= pivot) {
	    rightwall++;
	}
	/* now, spawn the next two iterations */
	{
	    struct qutil_aligned_qsort_iargs na[2];
	    aligned_t rets[2] = { 1, 1 };

	    na[0].array = array;
	    na[0].length = rightwall;
	    na[1].array = array + rightwall;
	    na[1].length = a->length - rightwall;
	    if (na[0].length > 0) {
		rets[0] = 0;
		/* future_fork((qthread_f)qutil_aligned_qsort_inner, na, rets); */
		/* qutil_aligned_qsort_inner(me, na); */
		qthread_fork((qthread_f) qutil_aligned_qsort_inner, &na[0], &rets[0]);
	    }
	    if (na[1].length > 0 && a->length > rightwall) {
		rets[1] = 0;
		/* future_fork((qthread_f)qutil_aligned_qsort_inner, na+1, rets+1); */
		/* qutil_aligned_qsort_inner(me, na+1); */
		qthread_fork((qthread_f) qutil_aligned_qsort_inner, &na[1], &rets[1]);
	    }
	    if (rets[0] == 0) {
		qthread_readFF(me, NULL, rets);
	    }
	    if (rets[1] == 0) {
		qthread_readFF(me, NULL, rets + 1);
	    }
	}
    }
    return 0;
}

void qutil_aligned_qsort(qthread_t * me, aligned_t *array, const size_t length)
{
    struct qutil_aligned_qsort_iargs arg;

    arg.array = array;
    arg.length = length;

    qutil_aligned_qsort_inner(me, &arg);
}
