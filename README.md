
# experimentation personal library


```
./bin/ceph_test_objectstore --gtest_filter="ObjectStore/StoreTest.Synthetic/1" --log_to_stderr=false --log_file=log.log --debug_bluestore=30 --plugin_dir=lib
cp profile.log ~/pb/
./stats

Results:
Function _kv_sync_thread:
                cycles: samples:                   20, avg:           3518369289 stdev     894760028.047122 p99          33426951686
        cpu_migrations: samples:                   17, avg:          20186220969 stdev     624304422.307552 p99          69268077172
          cache_misses: samples:                   37, avg:               220772 stdev        310825.073971 p99              1197985
         branch_misses: samples:                   37, avg:                46394 stdev         66744.329122 p99               246405
Function decode_some:
                cycles: samples:                21845, avg:                14970 stdev         17361.333877 p99                69419
          cache_misses: samples:                21846, avg:                  109 stdev           156.393734 p99                  592
         branch_misses: samples:                21845, avg:                   48 stdev            69.620399 p99                  313
Function decode_some:
                cycles: samples:                 9699, avg:                29868 stdev         25009.526705 p99               110450
          cache_misses: samples:                 9698, avg:                  221 stdev           199.484335 p99                  777
         branch_misses: samples:                 9699, avg:                  108 stdev           103.879738 p99                  456
Function queue_transactions:
                cycles: samples:                 8611, avg:              2684805 stdev      45093134.107070 p99             17484141
        cpu_migrations: samples:                   11, avg:             39464951 stdev      44859124.146431 p99            141977459
          cache_misses: samples:                 8622, avg:                 3963 stdev          3394.946097 p99                15047
         branch_misses: samples:                 8622, avg:                 1413 stdev          1290.311978 p99                 7319

```
