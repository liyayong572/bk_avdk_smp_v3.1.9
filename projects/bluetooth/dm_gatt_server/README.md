# DM GATTS示例，BLE数据传输和HID功能

| 支持的芯片 | BK7258 |
| --------- |  ------- |

[TOC]  

## 概述

本示例基于 蓝牙双模 ，演示了 BLE数据传输和HID控制。

## 编译
~~~
运行以下命令进行编译构建

cd sample_project/bluetooth/dm_gatt_server
export SDK_DIR=$HOME/../../bk_avdk_smp  //修改SDK_DIR为实际smp sdk路径
make clean
make bk7258

~~~
## 烧录测试操作方法
~~~
- 参考[如何烧录](/BURN.md)，烧录sample_project/bluetooth/dm_gatt_server/build/bk7258/dm_gatt_server/package/all-app.bin；
- 上电后，手机使用ble app可以搜索到bk7258_xxxx,点击链接，就可以完成链接。
- 测试控制拍照功能：
   打开摄像头，通过串口发送ap_cmd hogpd data_send命令，触发拍照。
- 数据收发测试：
   与手机app连接后，手机app打开notify enbale,通过手机APP发送write数据到bk7258,bk7258会把收到的数据打印出来；通过串口发送ap_cmd f618 data_send bb 10命令，bk7258会通过f618服务发送0x10个0xbb数据，通过串口发送ap_cmd fa00 data_send aa 10命令，bk7258会通过fa00服务发送0x10个0xaa数据。
- 数据接收测试：
  BLE app连接成功后，再properties为write的uuid下点击输入数据，bk7258收到数据会打印出来。

~~~


## 更多BLE开发指南请参考 [技术文档](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/ap_doc/bk7258/zh_CN/v3.1.1/developer-guide/bluetooth/bk_ble.html)。


## 故障排除

如有任何技术疑问，请联系我们FAE支持，或者在博通集成技术论坛上提交一个[问题](https://armino.bekencorp.com/)。我们会尽快回复您。
