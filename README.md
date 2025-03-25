# ThreadingSample

1. 日志类别为LogThreadingSample，运行时输出的日志（包括运行结果、错误、警告等）可搜索此类别进行过滤。

2. 相关操作：

   - 0：使用Async接口加载txt文件。
   - 1：启动/停止Runnable Thread。
   - 2：启动/停止FThread。
   - 3：使用ParallelFor进行纹理滤波操作（或关闭UI）。
   - 4：使用Task System进行纹理滤波操作（或关闭UI）。
   - 5：使用Task Graph System进行纹理滤波操作（或关闭UI）。
   - 6：使用Pipe进行纹理滤波操作（或关闭UI）。
   - 7：Queued Thread Pool示例。
   - 8：Queued Thread Pool Wrapper示例。
   - 9：Nested Task示例。
   - 数字键盘0：Low Level Task System示例。
   - 数字键盘1：使用UBlueprintAsyncActionBase创建自定义异步蓝图节点从网络下载图片并显示（或关闭UI）。
   - 数字键盘2：使用Dynamic Multicast Delegate + Task System进行纹理滤波操作（或关闭UI）。

3. 参数设置与调整：

   - 关卡蓝图中能设置的参数位于蓝图的Class Default中：

     <img src="Figs/LevelBPSettings.png" style="zoom:50%;" />

   - 关卡中的Actor可设置的参数如下：

     <img src="Figs/ActorSettings.png" style="zoom: 60%;" />

   - 其他参数可通过对应蓝图节点设置：

     <img src="Figs/BPFunctionSettings.png" style="zoom:50%;" />

4. 其他：

   - 示例工程中涉及到让线程休眠的操作，但这仅是为展示某些效果（比如在加载txt文件的例子中会在加载结束后让线程休眠若干时间以模拟加载时间很长的情况）。实际使用时请不要让线程休眠（除非你很清楚地知道自己这样做的后果）。
   - 示例工程仅展示相关接口及用法，可能存在不合理之处。
   - 代码组织：
     - C++：位于Source/ThreadingSample和Source/ThreadingSampleEditor。
     - 蓝图：位于StarterMap关卡蓝图中。

