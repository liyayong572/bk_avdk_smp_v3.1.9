# DM GATTC示例

| 支持的芯片 | BK7258 |
| --------- |  ------- |

[TOC]  

## 概述

本示例基于 蓝牙双模 ，演示了 BLE 主动扫描，主动发起连接从设备，连接完成验证数据收发。

## 编译
~~~
运行以下命令进行编译构建

cd sample_project/bluetooth/dm_gatt_client
export SDK_DIR=$HOME/../../bk_avdk_smp  //修改SDK_DIR为实际smp sdk路径
make clean
make bk7258

~~~
## 烧录测试操作方法
~~~
- 参考[如何烧录](/BURN.md)，烧录sample_project/bluetooth/dm_gatt_client/build/bk7258/dm_gatt_client/package/all-app.bin；
- 上电后，会主动发起搜索周边的ble设备，默认收到名称为BK7258_BLE的BLE广播包后会主动发起连接。

- 测试连接指定其他名称可以发命令：
   ap_cmd gattc con_dev_name BK7258_BLE，修改这个命令中的BK7258_BLE为你需要的。
- 数据发测试：
   连接成功后找到打印为write_handle= 的打印后面跟着就是可以通过这个handle 进行写数据；通过串口发送ap_cmd gattc data_write bc 20 xx 命令，bk7258会通过handle==xx发送0x20个0xbc的数据。
- 数据收测试：
  从机发数据过来，bk7258收到数据会打印出来，从机可以使用gatt_server这个工程来做测试。

~~~


## 更多BLE开发指南请参考 [技术文档](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/ap_doc/bk7258/zh_CN/v3.1.1/developer-guide/bluetooth/bk_ble.html)。


## 故障排除

如有任何技术疑问，请联系我们FAE支持，或者在博通集成技术论坛上提交一个[问题](https://armino.bekencorp.com/)。我们会尽快回复您。
