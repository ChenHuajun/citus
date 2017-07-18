/*-------------------------------------------------------------------------
 *
 * test/src/progress_utils.c
 *
 * This file contains functions to exercise progress monitoring functionality
 * within Citus.
 *
 * Copyright (c) 2017, Citus Data, Inc.
 *
 *-------------------------------------------------------------------------
 */


#include "postgres.h"
#include "miscadmin.h"
#include "fmgr.h"
#include "funcapi.h"

#include <unistd.h>

#include "distributed/multi_progress.h"
#include "nodes/execnodes.h"
#include "utils/tuplestore.h"


PG_FUNCTION_INFO_V1(create_progress);
PG_FUNCTION_INFO_V1(update_progress);
PG_FUNCTION_INFO_V1(finish_progress);
PG_FUNCTION_INFO_V1(show_progress);


Datum
create_progress(PG_FUNCTION_ARGS)
{
	uint64 magicNumber = PG_GETARG_INT64(0);
	uint64 stepCount = PG_GETARG_INT64(1);
	int * steps = (int *) CreateProgressMonitor(magicNumber, stepCount, sizeof(int), 0);

	int i = 0;
	for (; i < stepCount; i++)
	{
		steps[i] = 0;
	}

	PG_RETURN_VOID();
}


Datum
update_progress(PG_FUNCTION_ARGS)
{
	uint64 step = PG_GETARG_INT64(0);
	uint64 newValue = PG_GETARG_INT64(1);

	ProgressMonitorHeader *header;
	uint64 *steps;

	GetProgressMonitor(&header, (void **) &steps);

	steps[step] = newValue;

	PG_RETURN_VOID();
}


Datum
finish_progress(PG_FUNCTION_ARGS)
{
	FinalizeProgressMonitor();

	PG_RETURN_VOID();
}


Datum
show_progress(PG_FUNCTION_ARGS)
{
	uint64 magicNumber = PG_GETARG_INT64(0);
	List *attachedSegments = NIL;
	List *monitorList = ProgressMonitorList(magicNumber, &attachedSegments);
	Tuplestorestate *tupstore = NULL;
	TupleDesc tupdesc;
	MemoryContext perQueryContext;
	MemoryContext currentContext;
	ReturnSetInfo *resultSet = (ReturnSetInfo *) fcinfo->resultinfo;
	ListCell *monitorCell = NULL;

	/* check to see if caller supports us returning a tuplestore */
	if (resultSet == NULL || !IsA(resultSet, ReturnSetInfo))
	{
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("set-valued function called in context that cannot " \
						"accept a set")));
	}
	if (!(resultSet->allowedModes & SFRM_Materialize))
	{
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("materialize mode required, but it is not " \
						"allowed in this context")));
	}
	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
	{
		elog(ERROR, "return type must be a row type");
	}

	perQueryContext = resultSet->econtext->ecxt_per_query_memory;
	currentContext = MemoryContextSwitchTo(perQueryContext);
	tupstore = tuplestore_begin_heap(true, false, work_mem);
	resultSet->returnMode = SFRM_Materialize;
	resultSet->setResult = tupstore;
	resultSet->setDesc = tupdesc;
	MemoryContextSwitchTo(currentContext);

	elog(WARNING, "Number of monitors: %d", list_length(monitorList));

	foreach(monitorCell, monitorList)
	{
		void *monitorAddress = lfirst(monitorCell);
		ProgressMonitorHeader *header = NULL;
		uint64 *steps = NULL;
		int stepIndex = 0;

		MonitorInfoFromAddress(monitorAddress, &header, (void **) &steps);

		for (stepIndex = 0; stepIndex < header->stepCount; stepIndex++)
		{
			uint64 *step = steps + stepIndex;

			Datum values[2];
			bool nulls[2];

			MemSet(values, 0, sizeof(values));
			MemSet(nulls, 0, sizeof(nulls));

			values[0] = Int32GetDatum(stepIndex);
			values[1] = UInt64GetDatum(*step);

			tuplestore_putvalues(tupstore, tupdesc, values, nulls);
		}
	}

	tuplestore_donestoring(tupstore);

	DetachFromSegments(attachedSegments);

	return (Datum) 0;
}

