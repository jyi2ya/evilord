> **ARM 平台的 memory barrier**

傻逼华为，给的机器是啥鲲鹏的。

> 操作支持原文件带路径的情况，如 evenodd write path/to/file 5

暂时用 `simple_hash` 函数糊了个基本好用的东西上去，等 zwd 的优质哈希函数 :3

好耶，看起来效果很好，不打算修了！

> 取消 PMAX（质数 p 的最大值）的限制

决定不做了。

> 错误处理

做了题目明确要求的部分，应该不会做其他的了。

> 对计算部分的性能优化

做完力

引入了新的 `cpu_bound_cnt` 计数器后发现，在存取 2GB 大小的数据时有不少时间是 io 线程在等计算线程，所以需要优化计算部分的性能。

计数结果如下（read 与 repair 操作均为坏掉两块硬盘后进行）：

```plain
prime 3  
write blocked by cpu: 820/44739243, 0%
read blocked by cpu: 674/44739242, 0%
repair blocked by cpu: 354/44739243, 0%
prime 5  
write blocked by cpu: 222/13421773, 0%
read blocked by cpu: 1672987/13421772, 12%
repair blocked by cpu: 384335/13421773, 2%
prime 7  
write blocked by cpu: 7850/6391321, 0%
read blocked by cpu: 1337647/6391320, 20%
repair blocked by cpu: 2126070/6391321, 33%
prime 11  
write blocked by cpu: 105671/2440323, 4%
read blocked by cpu: 542118/2440322, 22%
repair blocked by cpu: 756978/2440323, 31%
prime 13  
write blocked by cpu: 193931/1720741, 11%
read blocked by cpu: 373879/1720740, 21%
repair blocked by cpu: 643455/1720741, 37%
prime 17  
write blocked by cpu: 234202/986896, 23%
read blocked by cpu: 113035/986895, 11%
repair blocked by cpu: 378740/986896, 38%
prime 19  
write blocked by cpu: 206914/784899, 26%
read blocked by cpu: 191675/784898, 24%
repair blocked by cpu: 346563/784899, 44%
prime 23  
write blocked by cpu: 135047/530505, 25%
read blocked by cpu: 149313/530504, 28%
repair blocked by cpu: 196190/530505, 36%
prime 29  
write blocked by cpu: 70606/330586, 21%
read blocked by cpu: 104768/330585, 31%
repair blocked by cpu: 133643/330586, 40%
prime 31  
write blocked by cpu: 59317/288641, 20%
read blocked by cpu: 128967/288640, 44%
repair blocked by cpu: 118223/288641, 40%
prime 37  
write blocked by cpu: 39016/201529, 19%
read blocked by cpu: 80314/201528, 39%
repair blocked by cpu: 82594/201529, 40%
prime 41  
write blocked by cpu: 31720/163681, 19%
read blocked by cpu: 67537/163680, 41%
repair blocked by cpu: 62232/163681, 38%
prime 43  
write blocked by cpu: 28522/148636, 19%
read blocked by cpu: 58948/148635, 39%
repair blocked by cpu: 61620/148636, 41%
prime 47  
write blocked by cpu: 22276/124161, 17%
read blocked by cpu: 51146/124160, 41%
repair blocked by cpu: 47246/124161, 38%
```

可以看出最坏情况（prime=31, read）下会有 44% 的时间是硬盘在等 cpu。
