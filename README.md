# MyShell

浙江大学课程综合实践II大作业(Project of ZJU Integrate Practice for Courses II)

## 设计需求

​	本题要求写一个简单的shell程序——MyShell。要求如下：

1. 支持以下命令：

   | 命令  | 解释                                                         |
   | ----- | ------------------------------------------------------------ |
   | bg    | 将挂起的作业移至后台运行                                     |
   | cd    | 改变当前目录，同时更新PWD环境变量                            |
   | clr   | 清屏                                                         |
   | dir   | 列出指定目录下的内容                                         |
   | echo  | 在屏幕上显示指定内容并换行                                   |
   | exec  | 执行外部程序。该命令会替换掉MyShell的代码段，执行成功后直接退出 |
   | exit  | 退出MyShell                                                  |
   | fg    | 将挂起或后台运行的作业转移至前台                             |
   | help  | 显示用户手册                                                 |
   | jobs  | 显示当前被挂起和后台运行的作业                               |
   | pwd   | 显示当前目录                                                 |
   | set   | 列出环境变量                                                 |
   | shift | 左移参数。\$0保持不变，\$1、\$2等变量被左移                  |
   | test  | 进行文件、数值、字符串测试                                   |
   | time  | 显示当前时间                                                 |
   | umask | 显示或改变umask的值                                          |
   | unset | 删除环境变量                                                 |
   
2. MyShell的环境变量应包含SHELL = \<PathName\>/MyShell，表示可执行程序MyShell的完整路径。

3. 其他命令行输入表示程序调用，程序的执行环境应包含环境变量PARENT = \<PathName\>/MyShell。

4. MyShell能够从文件中提取命令并执行，例如命令MyShell BatchFile表示从BatchFile中获取命令并执行。

5. 支持I/O重定向。且>表示创建输出文件，>>表示追加到文件末尾。

6. 支持后台运行。若命令末尾是&，则表示该命令在后台运行。MyShell在加载完该命令后必须立刻返回命令行提示符。

7. 支持管道符“|”。

8. 命令行提示符包含当前路径（即模仿bash的命令行提示符）。

## 扩展功能

​	除了题目要求的内容以外，笔者还额外实现了以下功能：

1. cd命令若不加参数，则表示进入到用户主目录。

2. echo命令支持变量引用。例如echo \$0，echo \${11}，echo \$PWD \$HOME等等。还可以用'\$#'显示全局参数个数。

3. set命令无参时表示列出所有环境变量，若有参数，则表示给\$1、\$2等变量赋值。

   例如执行set 0 1 2 3后，全局变量更新为：\$0不变，\$1=0，\$2=1，\$3=2，\$4=3。且set命令后面不一定必须加常量，也可以使用'\$'代表变量。例如set \$1 \$PWD $#。

4. test命令支持变量引用。例如test \$# -ge 3，test \$PWD = /home，test -x $0等等。

5. 支持快捷键操作。按Ctrl+C可以终止当前正在前台运行的命令。按Ctrl+Z可以挂起当前正在前台运行的命令。

6. 支持更多重定向。"0<"和"<"表示输入重定向。"1>"和">"表示覆盖模式的输出重定向，"1>>"和">>"表示追加模式的输出重定向。"2>"表示覆盖模式的错误信息重定向，"2>>"表示追加模式的错误信息重定向。

7. MyShell能够判断输入输出是否来自终端。当输入不是来自终端时，则不会输出命令行提示符，也不会即时打印已完成的后台作业信息。若输入来自终端，但输出被重定向到非终端（例如普通文件），MyShell仍会把已完成的后台作业信息即时输出到终端（而不是输出到重定向的文件）。
