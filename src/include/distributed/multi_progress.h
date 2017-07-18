/*-------------------------------------------------------------------------
 *
 * multi_progress.h
 *    Declarations for public functions and variables used in progress
 *    tracking functions in Citus.
 *
 * Copyright (c) 2017, Citus Data, Inc.
 *
 *-------------------------------------------------------------------------
 */

#ifndef MULTI_PROGRESS_H
#define MULTI_PROGRESS_H


#include "nodes/pg_list.h"


typedef struct ProgressMonitorHeader
{
	uint64 processId;
	int stepCount;
} ProgressMonitorHeader;


extern void * CreateProgressMonitor(uint64 progressTypeMagicNumber, int stepCount,
									Size stepSize, Oid relationId);
extern void GetProgressMonitor(ProgressMonitorHeader **header, void **steps);
extern void FinalizeProgressMonitor(void);
extern List * ProgressMonitorList(uint64 commandTypeMagicNumber, List **attachedSegments);
extern void MonitorInfoFromAddress(void *segmentAddress, ProgressMonitorHeader **header,
								   void **steps);
extern void DetachFromSegments(List *segmentList);


#endif /* MULTI_PROGRESS_H */
