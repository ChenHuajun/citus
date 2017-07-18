setup
{
    CREATE FUNCTION create_progress(bigint, bigint)
	RETURNS void
	AS 'citus'
	LANGUAGE C STRICT;

	CREATE FUNCTION update_progress(bigint, bigint)
	RETURNS void
	AS 'citus'
	LANGUAGE C STRICT;

	CREATE FUNCTION finish_progress()
	RETURNS void
	AS 'citus'
	LANGUAGE C STRICT;

	CREATE OR REPLACE FUNCTION show_progress(bigint)
	RETURNS TABLE(step int, progress bigint)
	AS 'citus'
	LANGUAGE C STRICT;
}

teardown
{
    DROP FUNCTION IF EXISTS create_progress(bigint, bigint);
	DROP FUNCTION IF EXISTS update_progress(bigint, bigint);
	DROP FUNCTION IF EXISTS finish_progress();
	DROP FUNCTION IF EXISTS show_progress(bigint);
}

session "s1"

step "s1-create-progress"
{
	SELECT create_progress(1337, 3);
}

step "s1-update-progress"
{
	SELECT update_progress(2, 78);
}

step "s1-finish-progress"
{
	SELECT finish_progress();
}

session "s2"

step "s2-create-progress"
{
	SELECT create_progress(1337, 5);
}

step "s2-update-progress"
{
	SELECT update_progress(4, 66);
}

step "s2-finish-progress"
{
	SELECT finish_progress();
}

session "s3"

step "s3-get-progress-info"
{
	SELECT * FROM show_progress(1337);
}

permutation "s1-create-progress" "s3-get-progress-info" "s1-finish-progress"
# permutation "s3-get-progress-info" "s1-create-progress" "s3-get-progress-info" "s1-update-progress" "s3-get-progress-info" "s2-create-progress" "s3-get-progress-info" "s2-update-progress" "s1-update-progress" "s3-get-progress-info" "s1-finish-progress" "s3-get-progress-info" "s2-finish-progress" "s3-get-progress-info"
