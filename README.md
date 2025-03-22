# HTTP chunked server

## 需求

- 用Transfer-Encoding: chunked向浏览器发送周期数据，同时支持处理其他请求
- 用C socket API
- 固定内存

## 设计

程序主体用select/poll，单线程。Non-blocking read，可以用pico http parser (Perl & MIT License)处理，后面也可以改成自己写parser

简单的实现就是只看POLLIN，在poll返回时检查时间，根据周期来设定poll的timeout，从而在达到周期的时候可以退出poll来检查时间，触发周期发送

关键的问题是，是否允许发送block？\
	如果用blocking API且真的阻塞 (比如TCP window full)，则其他连接无法通信\
	可以用write timeout解决这个问题，一旦timeout就RST\
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

bugs
发送完成后(zero chunk)，再次读取会断连
(不过正常应该没有主动发zero chunk的情况)

需要看看如何设计成BYOB，给应用层API来提供、修改buffer
	(修改buffer的API是为了接收文件)
