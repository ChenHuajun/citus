/*-------------------------------------------------------------------------
 *
 * multi_progress.c
 *	  Routines for tracking long-running jobs and seeing their progress.
 *
 * Copyright (c) 2017, Citus Data, Inc.
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "miscadmin.h"
#include "pgstat.h"

#include "distributed/multi_logical_optimizer.h"
#include "distributed/multi_progress.h"
#include "nodes/execnodes.h"
#include "storage/dsm.h"
#include "utils/builtins.h"


/* Dynamic shared memory handle of the current progress */
static uint64 currentProgressDSMHandle = DSM_HANDLE_INVALID;


static ReturnSetInfo * FunctionCallGetTupleStore1(PGFunction function, Oid functionId,
												  Datum argument);


/*
 * CreateProgressMonitor is used to create a place to store progress information related
 * to long running processes. The function creates a dynamic memory segment consisting of
 * a header regarding to the process and an array of "steps" that the long running
 * "operations" consists of. The handle of the dynamic shared memory is stored in
 * pg_stat_get_progress_info output, to be parsed by a progress retrieval command
 * later on. This behavior may cause unrelated (but hopefully harmless) rows in
 * pg_stat_progress_vacuum output. The caller of this function should provide a magic
 * number, a unique 64 bit unsigned integer, to distinguish different types of commands.
 */
void *
CreateProgressMonitor(uint64 progressTypeMagicNumber, int stepCount, Size stepSize,
					  Oid relationId)
{
	dsm_segment *dsmSegment = NULL;
	dsm_handle dsmHandle = 0;
	void *dsmSegmentAddress = NULL;
	ProgressMonitorHeader *header = NULL;
	void *steps = NULL;
	Size monitorSize = 0;

	if (stepSize <= 0 || stepCount <= 0)
	{
		ereport(ERROR,
				errmsg("Number of steps and size of each step should be positive "
					   "values."));
	}

	monitorSize = sizeof(ProgressMonitorHeader) + stepSize * stepCount;
	dsmSegment = dsm_create(monitorSize, DSM_CREATE_NULL_IF_MAXSEGMENTS);

	if (dsmSegment == NULL) {
		ereport(WARNING,
				errmsg("Couldn't create a dynamic shared memory segment to keep track of "
					   "progress of the current command."));
	}

	dsmSegmentAddress = dsm_segment_address(dsmSegment);
	dsmHandle = dsm_segment_handle(dsmSegment);

	MonitorInfoFromAddress(dsmSegmentAddress, &header, &steps);

	header->stepCount = stepCount;
	header->processId = MyProcPid;

	pgstat_progress_start_command(PROGRESS_COMMAND_VACUUM, relationId);
	pgstat_progress_update_param(1, dsmHandle);
	pgstat_progress_update_param(0, progressTypeMagicNumber);

	currentProgressDSMHandle = dsmHandle;

	return steps;
}


/*
 * GetProgressMonitor function returns the header and steps array related to the current
 * progress. A progress monitor should be created by calling CreateProgressMonitor,
 * before calling this function.
 */
void
GetProgressMonitor(ProgressMonitorHeader **header, void **steps)
{
	dsm_segment *dsmSegment = dsm_find_mapping(currentProgressDSMHandle);
	void *monitorAddress = NULL;

	if (dsmSegment == NULL)
	{
		dsmSegment = dsm_attach(currentProgressDSMHandle);
	}

	if (dsmSegment == NULL)
	{
		header = NULL;
		steps = NULL;
	}
	else
	{
		monitorAddress = dsm_segment_address(dsmSegment);
		MonitorInfoFromAddress(monitorAddress, header, steps);
	}
}


/*
 * FinalizeProgressMonitor releases the dynamic memory segment of the current progress
 * and removes the process from pg_stat_get_progress_info() output.
 */
void
FinalizeProgressMonitor(void)
{
	dsm_segment *segment = dsm_find_mapping(currentProgressDSMHandle);

	if (segment != NULL)
	{
		dsm_detach(segment);
	}

	currentProgressDSMHandle = DSM_HANDLE_INVALID;

	pgstat_progress_end_command();
}


/*
 * ProgressMonitorList returns the addresses of monitors of ongoing commands, associated
 * with the given identifier magic number. The function takes a pass in
 * pg_stat_get_progress_info output, filters the rows according to the given magic number,
 * and returns the list of addresses of dynamic shared memory segments. Notice that the
 * caller should parse each of the addresses with MonitorInfoFromAddress command and also
 * detach from the attached segments with a call to DetachFromSegments function.
 */
List *
ProgressMonitorList(uint64 commandTypeMagicNumber, List **attachedSegments)
{
	/*
	 * The expected magic number should reside in the first progress field and the
	 * actual segment handle in the second but the slot ordering is 1-indexed in the
	 * tuple table slot and there are 3 other fields before the progress fields in the
	 * pg_stat_get_progress_info output.
	 */
	const int magicNumberIndex = 0 + 1 + 3;
	const int dsmHandleIndex = 1 + 1 + 3;

	/*
	 * Currently, Postgres' progress logging mechanism supports only the VACUUM,
	 * operations. Therefore, we identify ourselves as a VACUUM command but only fill
	 * a couple of the available fields. Therefore the commands that use Citus' progress
	 * monitoring API will appear in pg_stat_progress_vacuum output.
	 */
	text *commandTypeText = cstring_to_text("VACUUM");
	Datum commandTypeDatum = PointerGetDatum(commandTypeText);
	Oid getProgressInfoFunctionOid = InvalidOid;
	TupleTableSlot *tupleTableSlot = NULL;
	ReturnSetInfo *progressResultSet = NULL;
	List *monitorAddressList = NIL;

	getProgressInfoFunctionOid = FunctionOid("pg_catalog",
											 "pg_stat_get_progress_info",
											 1);

	progressResultSet = FunctionCallGetTupleStore1(pg_stat_get_progress_info,
												   getProgressInfoFunctionOid,
												   commandTypeDatum);

	tupleTableSlot = MakeSingleTupleTableSlot(progressResultSet->setDesc);

	/* iterate over tuples in tuple store, and send them to destination */
	for (;;)
	{
		bool nextTuple = false;
		bool isNull = false;
		Datum magicNumberDatum = 0;
		uint64 magicNumber = 0;

		nextTuple = tuplestore_gettupleslot(progressResultSet->setResult,
											true,
											false,
											tupleTableSlot);

		if (!nextTuple)
		{
			break;
		}

		magicNumberDatum = slot_getattr(tupleTableSlot, magicNumberIndex, &isNull);
		magicNumber = DatumGetUInt64(magicNumberDatum);

		if (!isNull && magicNumber == commandTypeMagicNumber)
		{
			Datum dsmHandleDatum = slot_getattr(tupleTableSlot, dsmHandleIndex, &isNull);
			dsm_handle dsmHandle = DatumGetUInt64(dsmHandleDatum);
			dsm_segment *dsmSegment = dsm_find_mapping(dsmHandle);

			if (dsmSegment == NULL)
			{
				dsmSegment = dsm_attach(dsmHandle);
			}

			if (dsmSegment != NULL)
			{
				void *monitorAddress = dsm_segment_address(dsmSegment);
				*attachedSegments = lappend(*attachedSegments, dsmSegment);
				monitorAddressList = lappend(monitorAddressList, monitorAddress);
			}
			else
			{
				ereport(WARNING,
						errmsg("Operation details of a command couldn't be fetched."));
			}
		}

		ExecClearTuple(tupleTableSlot);
	}

	ExecDropSingleTupleTableSlot(tupleTableSlot);

	return monitorAddressList;
}


/*
 * MonitorInfoFromSegmentAddress returns the header and event array parts from the given
 * address.
 */
void
MonitorInfoFromAddress(void *segmentAddress, ProgressMonitorHeader **header, void **steps)
{
	ProgressMonitorHeader *headerAddress = (ProgressMonitorHeader *) segmentAddress;
	*header = headerAddress;
	*steps = (void *) (headerAddress + 1);
}


/*
 * DetachFromSegments ensures that the process is detached from all of the segments in
 * the given list.
 */
void
DetachFromSegments(List *segmentList)
{
	ListCell *segmentCell = NULL;

	foreach(segmentCell, segmentList)
	{
		dsm_segment *segment = (dsm_segment *) lfirst(segmentCell);

		dsm_detach(segment);
	}
}


/*
 * FunctionCallGetTupleStore1 calls the given set-returning PGFunction with the given
 * argument and returns the ResultSetInfo filled by the called function.
 */
static ReturnSetInfo *
FunctionCallGetTupleStore1(PGFunction function, Oid functionId, Datum argument)
{
	FunctionCallInfoData fcinfo;
	FmgrInfo flinfo;
	ReturnSetInfo *rsinfo = makeNode(ReturnSetInfo);
	EState *estate = CreateExecutorState();
	rsinfo->econtext = GetPerTupleExprContext(estate);
	rsinfo->allowedModes = SFRM_Materialize;

	fmgr_info(functionId, &flinfo);
	InitFunctionCallInfoData(fcinfo, &flinfo, 1, InvalidOid, NULL, (Node *) rsinfo);

	fcinfo.arg[0] = argument;
	fcinfo.argnull[0] = false;

	(*function)(&fcinfo);

	return rsinfo;
}
