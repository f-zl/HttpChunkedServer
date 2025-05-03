# HTTP chunked server

## 需求

- 用Transfer-Encoding: chunked向浏览器发送周期数据，同时支持处理其他请求
- 用C socket API
- 固定内存

## 设计

程序主体用select/poll，单线程。Non-blocking read，可以用pico http parser (Perl & MIT License)处理，后面也可以改成自己写parser

简单的实现就是只看POLLIN，在poll返回时检查时间，根据周期来设定poll的timeout，从而在达到周期的时候可以退出poll来检查时间，触发周期发送

关键的问题是，是否允许发送block？  
  如果用blocking API且真的阻塞 (比如TCP window full)，则其他连接无法通信  
  可以用write timeout解决这个问题，一旦timeout就RST  
  如果不用blocking API，则需要另外每个连接都需要一个独立buffer，数据也要先写到发送buffer里，在poll loop里循环发送

先用blocking API吧，简单点  
可以看看asio之类的库怎么设计的  
最好有C库的设计，比如libevent？libev？

```
void poll_loop() {
	for (;;) {
		if (has_listening_client) { // 存在监听周期数据的client
			timeout = calc_timeout(period, last_send_time)
		} else {
			timeout = -1
		}
		poll(timeout)
		if (error) { // unlikely
		} else if (is_timeout) {
			send_period_msg()
		} else {
			loop readable {
				// 如果当前连接是正在发chunked
response的状态，则不应该有数据，只会有关闭连接
				//
如果对方断开连接，poll会报告该连接为readable，然后recv返回0？
				//
是否可以强制浏览器复用连接？至少观测一下浏览器的行为
				process_data_input()
				if (is_request_complete) {
					response = process_request(request)
					send_response(response)
				}
			}
		}
	}
}
```

画下状态机

用fuzzy测试下

## Bugs

发送完成后(zero chunk)，再次读取会断连
(不过正常应该没有主动发zero chunk的情况)

需要看看如何设计成BYOB，给应用层API来提供、修改buffer  
  (修改buffer的API是为了接收文件)

处理完请求后收缓存没清空

## demo

写一个demo，实现以下功能

- 支持chunked response周期数据
- 支持在发送周期数据的同时进行其他操作
- 实现用户在接收文件时自己定义缓存区
- 文件传输时控制payload大小恰到好处 (这个是客户端要做的，应该就整个文件只发一个POST的性能最好？)

先实现基本功能

### demo设计

HTTP接口

- GET /periodic：返回chunked response，每秒钟发送一个累增的计数器值，u32LE
- GET /version：返回版本
- GET /param：读取参数。假设参数就是一个u32LE (比如当前计数器值就是日期参数吧)。u32LE
- POST /param：写参数：u32LE
- POST /image：上传固件，最后2字节是crc校验(LE)，用modbus CRC

经测试，浏览器的行为 (Edge)，对HTTP/1.1会用连接池，连接上会保持连接，用于传其他请求  
显然chucked response不能复用，所以一遍接收chunked response，一般发起其他请求会有2个请求  
如果chunked response关闭，然后再开启会如何，待测试  
嵌入式端行为如何，待测试

奇怪的是MSG_MORE不会缓存消息，而会直接发出。Wireshark显示每个回复都是发了多段。Why？  
说可能是TCP_NODELAY，但读出来值为0

### 设计发送缓存

为什么要发送缓存？  
  如果有多个连接，不允许某个连接阻塞在发送上，所以需要有发送缓存暂存待发数据
怎么设计？  
  每个连接一个固定长度的缓存 (对应用层够长)  
  应用层写数据先写缓存，在POLLOUT事件时执行发送  
  状态机管理POLLOUT事件的检查
应用层怎么写数据先写缓存？  
  应用层程序里不用send，而用send_to_buf  
  send_to_buf把数据memcpy到缓存，记录长度  
  在从buffer发送到OS时再减少缓存长度
怎么处理没有发送完成的情况？
  1. 可以规定缓存必须从首位开始，部分出队时剩余数据前移
  2. 可以环形缓存，每次都写入缓存的最后  
    send_to_buf也可能写到尾部，然后从头开始写

## TODO

- 完善单个Connection的状态机，图中加上所有状态变量
- 重构代码，或者改状态机，使两者完全对应
- 整理server连接的状态机，和代码对应

- 抽象buffer，实现应用层BYOB、应用层修改buffer
- 在其他服务的基础上实现文件传输，只在传输时用某个buffer
- 改为non-blocking send
- 对socket做抽象，兼容winsock
- 分层，event loop, protocol, user三层
- 如果用户需要一套代码支持多protocol，则需要动态回调
- 取名，接口加前缀
