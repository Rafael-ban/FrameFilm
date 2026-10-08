# Staged OTA host test

运行：`python firmware/frame_film_ark/tests/host_ota/run.py`；编译器不在 PATH 时加 `--cc <clang路径>`。依赖 Python 与 host C 编译器，无 ESP-IDF/设备依赖，产物只保存在临时目录并自动清理。

`run.py` 从实际 `service_ota.c` 的明确注释边界抽取新增暂存 OTA 函数，并由 `test_ota.c` include；没有复制生产算法。若生产边界变化，入口会失败，需要同步边界。

ESP OTA/分区与 mbedTLS API 是测试替身：摘要替身只用于驱动匹配/不匹配分支，**不是 SHA256 实现或密码学验证**。覆盖分区容量、长度/摘要错误、分块接收、芯片/项目拒绝、abort/重试与延后activate；不覆盖真实Flash、mbedTLS、HTTP/WiFi、BLE应答时序或重启。先前临时harness已通过；本次整理为持久入口后按要求未再次运行。
