# 验证范围与负责人

| 负责人 | 文件范围 | 二进制 |
|---|---|---|
| radar_data_reader | modules/data_reader、tests/data_reader_test.cpp | radar_data_reader.dll |
| radar_playback | modules/playback、tests/playback_test.cpp | radar_playback.dll |
| radar_frontend | modules/frontend、tests/frontend_test.cpp | radar_frontend.dll |
| 根代理 | 公共接口、应用连接、构建、真实数据测试、SDK示例、交付 | radar_contracts.dll、radar-playback.exe、ZIP |

一名负责人修改一个业务模块，公共交接接口先固定。根代理独立编译、读取报告、运行全部序列并检查实际截图。

模块测试必须检查值、错误和资源边界：239/240分页、末帧、非法页面/图像、原文件保护、原子导出、缓存淘汰、线程归属；过期跳转结果、暂停、倍速期限、不积压读请求、末帧和重新播放；真实按钮参数、全部帧范围、图像像素/比例、显示历史上限和实际Widget绘制。

实际程序测试由tests/process_test.py执行，报告保留在build/process-evidence下，每次使用新目录。全量导出逐个比较7203帧的28个位姿值和14个误差值；全量解码逐帧实际读取两张JPEG，不能只检查文件存在。图片检查和读线程结果都来自编译后的EXE/DLL。

交付再以仅包含程序运行目录和Windows System32的PATH执行，清除QTDIR和QT_PLUGIN_PATH，确认不会意外依赖SDK。独立调用示例只使用公开接口和contracts导入库，动态解析三个模块工厂并用真实数据完成首末帧显示与CSV导出。

完整包必须有7203帧；AppOnly构建明确不附数据，供CI或复用已有数据包。CI使用现有240帧片段，不代表7203帧完整验证。完整制品的manifest和test-evidence必须与实际数据范围一致。
