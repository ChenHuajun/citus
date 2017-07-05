# create hash distributed table to test behavior of COPY in concurrent operations
setup
{
	SET citus.shard_replication_factor TO 1;
    CREATE TABLE hash_copy(id integer, data text);
	SELECT create_distributed_table('hash_copy', 'id');
	COPY hash_copy FROM PROGRAM 'echo 0, a\\n1, b\\n2, c\\n3, d\\n4, e' WITH CSV;
}

# drop distributed table
teardown
{
    DROP TABLE IF EXISTS hash_copy CASCADE;
}

# session 1
session "s1"
step "s1-begin" { BEGIN; }
step "s1-copy" { COPY hash_copy FROM PROGRAM 'echo 5, f\\n6, g\\n7, h\\n8, i\\n9, j' WITH CSV; }
step "s1-copy-additional-column" { COPY hash_copy FROM PROGRAM 'echo 5, f, 5\\n6, g, 6\\n7, h, 7\\n8, i, 8\\n9, j, 9' WITH CSV; }
step "s1-router-select" { SELECT * FROM hash_copy WHERE id = 1; }
step "s1-multi-shard-select" { SELECT * FROM hash_copy ORDER BY id, data; }
step "s1-select-count" { SELECT COUNT(*) FROM hash_copy; }
step "s1-insert" { INSERT INTO hash_copy VALUES(0, 'k'); }
step "s1-update" { UPDATE hash_copy SET data = 'l' WHERE id = 0; }
step "s1-delete" { DELETE FROM hash_copy WHERE id = 1; }
step "s1-truncate" { TRUNCATE hash_copy; }
step "s1-drop" { DROP TABLE hash_copy; }
step "s1-ddl-create-index" { CREATE INDEX hash_copy_index ON hash_copy(id); }
step "s1-ddl-drop-index" { DROP INDEX hash_copy_index; }
step "s1-ddl-add-column" { ALTER TABLE hash_copy ADD new_column int DEFAULT 0; }
step "s1-ddl-drop-column" { ALTER TABLE hash_copy DROP new_column; }
step "s1-ddl-rename-column" { ALTER TABLE hash_copy RENAME data TO new_data; }
step "s1-table-size" { SELECT citus_table_size('hash_copy'); SELECT citus_relation_size('hash_copy'); SELECT citus_total_relation_size('hash_copy'); }
step "s1-master-modify-multiple-shards" { SELECT master_modify_multiple_shards('DELETE FROM hash_copy;'); }
step "s1-create-non-distributed-table" { CREATE TABLE hash_copy(id integer, data text); COPY hash_copy FROM PROGRAM 'echo 0, a\\n1, b\\n2, c\\n3, d\\n4, e' WITH CSV; }
step "s1-distribute-table" { SELECT create_distributed_table('hash_copy', 'id'); }
step "s1-commit" { COMMIT; }

# session 2
session "s2"
step "s2-copy" { COPY hash_copy FROM PROGRAM 'echo 5, f\\n6, g\\n7, h\\n8, i\\n9, j' WITH CSV; }
step "s2-copy-additional-column" { COPY hash_copy FROM PROGRAM 'echo 5, f, 5\\n6, g, 6\\n7, h, 7\\n8, i, 8\\n9, j, 9' WITH CSV; }
step "s2-router-select" { SELECT * FROM hash_copy WHERE id = 1; }
step "s2-multi-shard-select" { SELECT * FROM hash_copy ORDER BY id, data; }
step "s2-insert" { INSERT INTO hash_copy VALUES(0, 'k'); }
step "s2-update" { UPDATE hash_copy SET data = 'l' WHERE id = 0; }
step "s2-delete" { DELETE FROM hash_copy WHERE id = 1; }
step "s2-truncate" { TRUNCATE hash_copy; }
step "s2-drop" { DROP TABLE hash_copy; }
step "s2-ddl-create-index" { CREATE INDEX hash_copy_index ON hash_copy(id); }
step "s2-ddl-drop-index" { DROP INDEX hash_copy_index; }
step "s2-ddl-create-index-concurrently" { CREATE INDEX CONCURRENTLY hash_copy_index ON hash_copy(id); }
step "s2-ddl-add-column" { ALTER TABLE hash_copy ADD new_column int DEFAULT 0; }
step "s2-ddl-drop-column" { ALTER TABLE hash_copy DROP new_column; }
step "s2-ddl-rename-column" { ALTER TABLE hash_copy RENAME data TO new_data; }
step "s2-table-size" { SELECT citus_table_size('hash_copy'); SELECT citus_relation_size('hash_copy'); SELECT citus_total_relation_size('hash_copy'); }
step "s2-master-modify-multiple-shards" { SELECT master_modify_multiple_shards('DELETE FROM hash_copy;'); }
step "s2-create-non-distributed-table" { CREATE TABLE hash_copy(id integer, data text); COPY hash_copy FROM PROGRAM 'echo 0, a\\n1, b\\n2, c\\n3, d\\n4, e' WITH CSV; }
step "s2-distribute-table" { SELECT create_distributed_table('hash_copy', 'id'); }

# permutations - COPY vs COPY
permutation "s1-begin" "s1-copy" "s2-copy" "s1-commit" "s1-select-count"

# permutations - COPY first
permutation "s1-begin" "s1-copy" "s2-router-select" "s1-commit" "s1-select-count"
permutation "s1-begin" "s1-copy" "s2-multi-shard-select" "s1-commit" "s1-select-count"
permutation "s1-begin" "s1-copy" "s2-insert" "s1-commit" "s1-select-count"
permutation "s1-begin" "s1-copy" "s2-update" "s1-commit" "s1-select-count"
permutation "s1-begin" "s1-copy" "s2-delete" "s1-commit" "s1-select-count"
permutation "s1-begin" "s1-copy" "s2-truncate" "s1-commit" "s1-select-count"
permutation "s1-begin" "s1-copy" "s2-drop" "s1-commit" "s1-select-count"
permutation "s1-begin" "s1-copy" "s2-ddl-create-index" "s1-commit" "s1-select-count"
permutation "s1-ddl-create-index" "s1-begin" "s1-copy" "s2-ddl-drop-index" "s1-commit" "s1-select-count"
permutation "s1-begin" "s1-copy" "s2-ddl-create-index-concurrently" "s1-commit" "s1-select-count"
permutation "s1-begin" "s1-copy" "s2-ddl-add-column" "s1-commit" "s1-select-count"
permutation "s1-ddl-add-column" "s1-begin" "s1-copy-additional-column" "s2-ddl-drop-column" "s1-commit" "s1-select-count"
permutation "s1-begin" "s1-copy" "s2-table-size" "s1-commit" "s1-select-count"
permutation "s1-begin" "s1-copy" "s2-master-modify-multiple-shards" "s1-commit" "s1-select-count"
permutation "s1-drop" "s1-create-non-distributed-table" "s1-begin" "s1-copy" "s2-distribute-table" "s1-commit" "s1-select-count"

# permutations - COPY second
permutation "s1-begin" "s1-router-select" "s2-copy" "s1-commit" "s1-select-count"
permutation "s1-begin" "s1-multi-shard-select" "s2-copy" "s1-commit" "s1-select-count"
permutation "s1-begin" "s1-insert" "s2-copy" "s1-commit" "s1-select-count"
permutation "s1-begin" "s1-update" "s2-copy" "s1-commit" "s1-select-count"
permutation "s1-begin" "s1-delete" "s2-copy" "s1-commit" "s1-select-count"
permutation "s1-begin" "s1-truncate" "s2-copy" "s1-commit" "s1-select-count"
permutation "s1-begin" "s1-drop" "s2-copy" "s1-commit" "s1-select-count"
permutation "s1-begin" "s1-ddl-create-index" "s2-copy" "s1-commit" "s1-select-count"
permutation "s1-ddl-create-index" "s1-begin" "s1-ddl-drop-index" "s2-copy" "s1-commit" "s1-select-count"
permutation "s1-begin" "s1-ddl-add-column" "s2-copy" "s1-commit" "s1-select-count"
permutation "s1-ddl-add-column" "s1-begin" "s1-ddl-drop-column" "s2-copy" "s1-commit" "s1-select-count"
permutation "s1-begin" "s1-table-size" "s2-copy" "s1-commit" "s1-select-count"
permutation "s1-begin" "s1-master-modify-multiple-shards" "s2-copy" "s1-commit" "s1-select-count"
permutation "s1-drop" "s1-create-non-distributed-table" "s1-begin" "s1-distribute-table" "s2-copy" "s1-commit" "s1-select-count"