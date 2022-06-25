//程序名：MyShell
//作者学号：尤锦江 3190102352


#include <iostream>
#include <string>
#include <sstream>
#include <ctime>
#include <unistd.h>
#include <dirent.h>
#include <pwd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
using namespace std;


//---------------------------------------------------------全局变量---------------------------------------------------------

//存储当前指令
char buffer[1024] = {0};
string CurrentCMD;
//输入输出控制相关
bool InIsTerminal; //输入来自终端. 只有为真时，才会输出子进程表和提示字符串
bool OutIsTerminal;//输出送至终端.
int TerminalIn; //终端输入的文件描述符，未必等于STDIN_FILENO，因为标准输入有可能被重定向
int TerminalOut;//终端输出的文件描述符，未必等于STDOUT_FILENO，因为标准输出有可能被重定向
//返回信息变量
int State;//0: 执行成功; 1:执行错误
string Message1;//输出内容
string Message2;//错误信息
//部分环境变量
string HostName;//主机名
string UserName;//用户名
string HomeDir; //用户主目录
string ShellPath;//MyShell的路径
string HelpPath;//帮助手册的路径
string PWD;     //当前路径
int PID;    //进程号
int SubPID; //子进程进程号，若当前无子进程则设为-1
int argc; string argv[1024];//参数个数和参数值，分别对应$#, $0, $1, $2, ...
//子进程表
const int MaxWork = 1024;
int Jobs[MaxWork]; //子进程的pid
int States[MaxWork];//子进程的状态. 0: 空. 1: 后台运行. 2: 被挂起
string CMDInfo[MaxWork];//子进程执行的命令
int Front, Rear;//表头指针和表尾指针


//---------------------------------------------------------辅助函数---------------------------------------------------------

//快排模板
template <class T>
static void QuickSort(T Arr[], int l, int r){
    if (l >= r) return;
    T mid = Arr[(l+r)/2];
    int i = l, j = r;
    while (i <= j){
        while (Arr[i] < mid) i++;
        while (Arr[j] > mid) j--;
        if (i <= j){
            T temp = Arr[i];
            Arr[i] = Arr[j];
            Arr[j] = temp;
            i++; j--;
        }
    }
    QuickSort(Arr, l, j);
    QuickSort(Arr, i, r);
}

//将字符串转换为整数. 若无法转换则抛出异常
int StringToInt(const string& n);

//解析"$0", "$1", "${9}", "${31}"这种字符串，返回其所代表的数值。例如输入"$9"则返回9. 若不合法则返回-1
int GetParamIndex(const string& cmd);

//根据字符串var，取得对应变量的值. 例如var="$HOME"时，返回变量HOME对应的值
string GetValue(const string &var);

//比较两个timespec类型的变量。-1：小于；0：等于；1：大于
int TimeCMP(const timespec& t1, const timespec& t2);

//返回子进程信息连接而成的字符串
string JobString(int JobIndex, int Finished = 0);


//---------------------------------------------------------单个指令实现---------------------------------------------------------

//bg命令，将被挂起的工作转至后台运行
void _bg(string cmd[], int ParaNum);

//cd命令. 若无参数则进入主目录, 否则进入参数对应的目录
void _cd(string cmd[], int ParaNum);

//clr清屏
void _clr(string cmd[], int ParaNum);

//dir列出指定文件夹内的文件
void _dir(string cmd[], int ParaNum);

//echo打印内容到屏幕
void _echo(string cmd[], int ParaNum);

//执行exec指令
void _exec(string cmd[], int ParaNum);

//exit退出程序
void _exit(string cmd[], int ParaNum);

//fg将后台作业移至前台执行
void _fg(string cmd[], int ParaNum);

//help输出帮助信息
void _help(string cmd[], int ParaNum);

//jobs输出当前正在执行或被挂起的任务
void _jobs(string cmd[], int ParaNum);

//pwd列出当前目录
void _pwd(string cmd[], int ParaNum);

//set命令列出当前所有环境变量，或设置全局变量的值
void _set(string cmd[], int ParaNum);

//shift命令，左移全局变量
void _shift(string cmd[], int ParaNum);

//test命令
void _test(string cmd[], int ParaNum);

//time列出当前时间
void _time(string cmd[], int ParaNum);

//umask输出屏蔽字或更改屏蔽字的值
void _umask(string cmd[], int ParaNum);

//unset命令删除环境变量
void _unset(string cmd[], int ParaNum);


//---------------------------------------------------------重要函数---------------------------------------------------------

//初始化
void Init(int Argc, char * Argv[]);

//信号处理函数，用于处理Ctrl+C、Ctrl+Z等组合键
void SignalProcess(int Signal);

//第一层解析指令：处理"&"后台运行符号，其余交给ExecMultipCMD函数
void Exec(string CMD);

//第二层解析指令：执行多条用管道符分隔的指令。单条指令交给ExecSingleCMD函数
void ExecMultipCMD(string cmd[], int ParaNum, bool WithFork = true);

//第三层解析指令：执行单条指令，可能包含重定向
void ExecSingleCMD(string cmd[], int ParaNum, bool WithFork = true);


//---------------------------------------------------------main函数---------------------------------------------------------

int main(int Argc, char * Argv[]){
    //初始化
    Init(Argc, Argv);
    //循环处理指令系统默认
    int Flag = 1;
    while(Flag){
        //如果是在控制台界面下，则需要输出提示字符
        if (InIsTerminal){
            sprintf(buffer, "\e[1;32m%s@%s\e[0m:\e[1;34m%s\e[0m$ ",
                    UserName.c_str(),
                    HostName.c_str(),
                    PWD.c_str());
            string TempStr = buffer;
            write(TerminalOut, TempStr.c_str(), TempStr.length());//注意：提示字符串只输出到终端，不能输出到STDOUT！！！！！
        }
        //读取一行，遇到换行符或EOF则结束
        int i;
        for (i = 0; ; i++){
            if (read(STDIN_FILENO, buffer + i, 1) <= 0){//读到EOF
                Flag = 0;
                break;
            }
            if (buffer[i] == '\n')
                break;
        }
        buffer[i] = '\0';
        //执行指令
        Exec(buffer);
    }
}


//---------------------------------------------------------函数实现---------------------------------------------------------

//将字符串转换为整数. 若无法转换则抛出异常
int StringToInt(const string& n){
    int Sign = 1, Magn = 0, i = 0;
    while (n[i] == '+' || n[i] == '-'){
        if (n[i] == '-') Sign = -Sign;
        i++;
    }
    if (n[i] == '\0') throw invalid_argument("Not a integer.");
    for (; n[i]; i++)
        if (n[i] <= '9' && n[i] >= '0')
            Magn = Magn * 10 + n[i] - '0';
        else throw invalid_argument("Not a integer.");
    return Sign * Magn;
}

//解析"$0", "$1", "${9}", "${31}"这种字符串，返回其所代表的数值。例如输入"$9"则返回9. 若不合法则返回-1
int GetParamIndex(const string& cmd){
    int ans = 0;
    if (cmd.length() <= 1 || cmd[0] != '$') return -1;
    if (cmd[1] == '{'){
        if (cmd.length() <= 3 || cmd[cmd.length() - 1] != '}') return -1;
        for (int i = 2; i < cmd.length() - 1; i++)
            if (cmd[i] >= '0' && cmd[i] <= '9')
                ans = ans * 10 + cmd[i] - '0';
            else return -1;
    }else{
        for (int i = 1; i < cmd.length(); i++)
            if (cmd[i] >= '0' && cmd[i] <= '9')
                ans = ans * 10 + cmd[i] - '0';
            else return -1;
    }
    return ans;
}

//根据字符串var，取得对应变量的值. 例如var="$HOME"时，返回变量HOME对应的值
string GetValue(const string &var){
    if (var[0] == '$'){//以$开头，替换成变量的值
        int ParaIndex;
        if ((ParaIndex = GetParamIndex(var)) >= 0){//进一步判断变量名是否是$0, $1这种名字
            if (ParaIndex < argc)
                return argv[ParaIndex];
            else return "";
        }else if (var == "$#")//$#表示参数个数
            return to_string(argc - 1);
        else{//否则，利用getenv获得变量的值
            char * str = getenv(var.substr(1).c_str());
            if (str != NULL)//环境变量存在
                return str;
            else return "";
        }
    } else //否则，不以$开头，则直接返回原字符串
        return var;
}

//比较两个timespec类型的变量。-1：小于；0：等于；1：大于
int TimeCMP(const timespec& t1, const timespec& t2){
    if (t1.tv_sec < t2.tv_sec)
        return -1;
    if (t1.tv_sec > t2.tv_sec)
        return 1;
    if (t1.tv_nsec < t2.tv_nsec)
        return -1;
    if (t1.tv_nsec > t2.tv_nsec)
        return 1;
    return 0;
}

//输出子进程信息
string JobString(int JobIndex, int Finished){
    if (JobIndex < Front || JobIndex >= Rear || States[JobIndex] != 1 && States[JobIndex] != 2) return "";
    return"[" + to_string(JobIndex + 1) + "]\t" + to_string(Jobs[JobIndex]) + "\t\t" + ((Finished) ? "Finish" : (States[JobIndex] == 1) ? "Running" : "Hanging") + "\t\t" + CMDInfo[JobIndex] + "\n";
}

//bg命令，将被挂起的工作转至后台运行
void _bg(string cmd[], int ParaNum){
    //无参，列出所有正在后台运行的进程
    if (ParaNum == 0){
        Message1 = "";
        Message2 = "";
        for (int i = Front; i < Rear; i++)
            if (States[i] == 1)
                Message1 += JobString(i);
        if (Message1 == "") Message1 = "bg: No mission is running at the background.\n";
        State = 0;
        return;
    }
    //根据参数列表，把对应的任务移至后台运行
    Message1 = "";
    Message2 = "";
    State = 0;
    for (int i = 0; i < ParaNum; i++){
        int WorkID;
        //将字符串转换为整数
        try{
            WorkID = StringToInt(cmd[i]);
        }catch(...){
            Message1 += "bg: " + cmd[i] + ": Not a valid positive integer.\n";
            continue;
        }
        //若不存在指定作业号的任务
        if (WorkID > Rear || WorkID <= Front || States[WorkID - 1] == 0){
            Message1 += "bg: " + cmd[i] + ": No mission.\n";
            continue;
        }
        //若已经在后台运行
        if (States[WorkID - 1] == 1){
            Message1 += "bg: " + cmd[i] + ": Already running at the background.\n";
            continue;
        }
        //更新Jobs表并发送信号
        States[WorkID - 1] = 1;
        kill(Jobs[WorkID - 1], SIGCONT);
    }
}

//cd命令. 若无参数则进入主目录, 否则进入参数对应的目录
void _cd(string cmd[], int ParaNum){
    if (ParaNum == 0 || ParaNum == 1 && cmd[0] == "~"){//chdir不支持"~"作为路径, 所以需要单独判断
        //进入用户主目录
        chdir(HomeDir.c_str());
        PWD = HomeDir;
        //更新PWD环境变量
        setenv("PWD", PWD.c_str(), 1);
        Message1 = "";
        Message2 = "";
        State = 0;
    }else if (ParaNum == 1){//参数为1, 直接调用chdir改变路径
        if(chdir(cmd[0].c_str())){//chdir调用失败
            Message1 = "";
            Message2 = "cd: Unable to change directory to " + cmd[0] + "\n";
            State = 1;
        }else{//chdir调用成功
            char buffer[1024];
            getcwd(buffer, 1024);
            PWD = buffer;
            //更新PWD环境变量
            setenv("PWD", buffer, 1);
            Message1 = "";
            Message2 = "";
            State = 0;
        }
    }else{//参数过多，报错
        Message1 = "";
        Message2 = "cd: Too many parameters.\n";
        State = 1;
    }
}

//clr清屏
void _clr(string cmd[], int ParaNum){
    if (ParaNum > 0){//参数过多，报错
        Message1 = "";
        Message2 = "clr: Too many parameters.\n";
        State = 1;
        return;
    }
    system("clear");
    Message1 = "";
    Message2 = "";
    State = 0;
}

//dir列出指定文件夹内的文件
void _dir(string cmd[], int ParaNum){
    if (ParaNum >= 2){//参数过多，报错
        Message1 = "";
        Message2 = "dir: Too many parameters.\n";
        State = 1;
    }else{
        if (ParaNum == 0) cmd[0] = PWD;//若无参，则列出当前目录下的文件. 否则，列出参数指定的目录下的文件
        DIR *pDir;
        struct dirent* ptr;
        //打开目录
        if(!(pDir = opendir(cmd[0].c_str()))){
            Message1 = "";
            Message2 = "dir: Folder " + cmd[0] + " doesn't Exist.\n";
            State = 1;
            return;
        }
        string Files[1024]; int Num = 0;
        //读取文件信息
        while((ptr = readdir(pDir)) != NULL)
            Files[Num++] = ptr->d_name;
        closedir(pDir);
        //对结果排序
        QuickSort(Files, 0, Num - 1);
        Message1 = "";
        Message2 = "";
        State = 0;
        //前两个文件分别是.和..，不用输出
        for (int i = 2; i< Num; i++)
            Message1 = Message1 + Files[i] + "\n";
    }
}

//echo打印内容到屏幕
void _echo(string cmd[], int ParaNum){
    Message1 = "";
    Message2 = "";
    State = 0;
    //遍历每个参数
    for (int i = 0; i < ParaNum; i++){
        string Value = GetValue(cmd[i]);
        if (Value != "") Message1 += Value + " ";
    }
    //换行
    if (Message1.length()) Message1[Message1.length() - 1] = '\n';
    else Message1 = "\n";
}

//执行exec指令
void _exec(string cmd[], int ParaNum){
    if (ParaNum == 0){
        Message1 = "";
        Message2 = "exec: Expected at least a parameter.\n";
        State = 1;
        return;
    }
    //设置参数
    char * arg[ParaNum + 1];
    for (int i = 0; i < ParaNum; i++)
        arg[i] = const_cast<char *>(cmd[i].c_str());
    arg[ParaNum] = NULL;
    Message1 = "";
    Message2 = "";
    State = 0;
    //调用execvp函数
    execvp(cmd[0].c_str(), arg);
    //exec执行成功后会退出源程序。如果能执行到下面的语句，说明exec出错
    Message1 = "";
    Message2 = "exec: Fail to execute " + cmd[0] + ".\n";
    State = 1;
}

//exit退出程序
void _exit(string cmd[], int ParaNum){
    Message1 = "";
    Message2 = "";
    State = 0;
    exit(0);
}

//fg将后台作业移至前台执行
void _fg(string cmd[], int ParaNum){
    Message1 = "";
    Message2 = "";
    State = 0;
    //无参
    if (ParaNum == 0){
        Message1 = "";
        Message2 = "fg: Please input the mission ID.\n";
        State = 1;
        return;
    }
    //多于一个参数
    if (ParaNum >= 2){
        Message1 = "";
        Message2 = "fg: Too many parameters.\n";
        State = 1;
        return;
    }
    //根据参数列表，把对应的任务移至前台运行
    int WorkID;
    try{
        WorkID = StringToInt(cmd[0]);
    }catch(...){
        Message1 = "";
        Message2 = "fg: " + cmd[0] + ": Not a valid positive integer.\n";
        State = 1;
        return;
    }
    if (WorkID > Rear || WorkID <= Front || States[WorkID - 1] == 0){
        Message1 = "";
        Message2 = "fg: " + cmd[0] + ": No mission.\n";
        State = 1;
        return;
    }
    Message1 = "";
    Message2 = "";
    State = 0;
    CurrentCMD = CMDInfo[WorkID - 1];
    if (InIsTerminal){//把指令内容输出到屏幕，告知用户前台作业的信息
        write(TerminalOut, CMDInfo[WorkID - 1].c_str(), CMDInfo[WorkID - 1].length());
        write(TerminalOut, "\n", 1);
    }
    //更新Jobs表
    States[WorkID - 1] = 0;
    SubPID = Jobs[WorkID - 1];
    if (Rear == WorkID && Front == WorkID - 1) Front = Rear = 0;
    else if (Front == WorkID - 1) Front++;
    else if (Rear == WorkID) Rear--;
    //发送信号
    setpgid(SubPID, getgid());//设置进程组，使子进程进入前台进程组
    kill(SubPID, SIGCONT);
    //等待子进程完成
    while (SubPID != -1 && !waitpid(SubPID, NULL, WNOHANG));
    SubPID = -1;
}

//help输出帮助信息
void _help(string cmd[], int ParaNum){
    if (ParaNum >= 2){
        Message1 = "";
        Message2 = "help: Too many parameters.\n";
        State = 1;
        return;
    }
    string Target;
    if (ParaNum == 0) Target = "global";//若无参, 输出全局帮助手册
    else Target = cmd[0];//否则输出对应指令的帮助手册

    FILE * fp = fopen(HelpPath.c_str(), "r");
    if (fp == NULL){
        Message1 = "";
        Message2 = "help: Help manual file is not found.\n";
        State = 1;
        return;
    }
    while(1){
        char buf[1024];
        fgets(buf, 1024, fp); for(int i = 0; buf[i] || (buf[i - 2] = '\0'); i++);
        if (buf[0] == '#' && buf[1] == '#' && buf[2] == '#'){//"###"是分隔符
            if (buf[3] == '\0'){
                Message1 = "";
                Message2 = "help: There's no help manual for " + Target + ".\n";
                State = 1;
                fclose(fp);
                return;
            }
            if (Target == buf + 3) break;
        }
    }
    Message1 = "";
    Message2 = "";
    State = 0;
    while(1){
        char buf[1024];
        fgets(buf, 1024, fp); for(int i = 0; buf[i] || (buf[i - 2] = '\0'); i++);
        if (buf[0] == '#' && buf[1] == '#' && buf[2] == '#')
            break;
        else
            Message1 = Message1 + buf + "\n";
    }
    fclose(fp);
}

//jobs输出当前正在执行或被挂起的任务
void _jobs(string cmd[], int ParaNum){
    if (ParaNum > 0){
        Message1 = "";
        Message2 = "jobs: Too many parameters.\n";
        State = 1;
        return;
    }
    Message1 = "";
    for (int i = Front; i < Rear; i++)
        Message1 += JobString(i);
    Message2 = "";
    State = 0;
}

//pwd列出当前目录
void _pwd(string cmd[], int ParaNum){
    if (ParaNum > 0){//参数过多，报错
        Message1 = "";
        Message2 = "pwd: Too many parameters.\n";
        State = 1;
        return;
    }
    Message1 = PWD + "\n";
    Message2 = "";
    State = 0;
}

//set命令列出当前所有环境变量，或设置全局变量的值
void _set(string cmd[], int ParaNum){
    Message1 = "";
    Message2 = "";
    State = 0;
    extern char** environ;//环境变量表
    if (ParaNum > 0){//参数不为0，则改变全局变量
        string TempArgv[1024];
        int TempArgc;
        //根据string cmd[]，确定参数的值和个数，存入TempArgv和TempArgc
        for (TempArgc = 1; TempArgc <= 1023 && TempArgc <= ParaNum; TempArgc++)
            TempArgv[TempArgc] = GetValue(cmd[TempArgc - 1]);
        //更新argc
        argc = TempArgc;
        //更新argv
        for (int i = 1; i < TempArgc; i++)
            argv[i] = TempArgv[i];
    } else {//参数个数为0，则输出所有环境变量
        for(int i = 0; environ[i] != NULL; i++)
            Message1 = Message1 + environ[i] + "\n";
    }
}

//shift命令，左移全局变量
void _shift(string cmd[], int ParaNum){
    if (ParaNum >= 2){
        Message1 = "";
        Message2 = "shift: Too many parameters.\n";
        State = 1;
    }else{
        if (ParaNum == 0) cmd[0] = "1"; //若无参，则默认左移1次
        int cnt;
        try{
            cnt = StringToInt(cmd[0]);
            if (cnt < 0) throw invalid_argument("");;
        }catch(...){
            Message1 = "";
            Message2 = "shift: " + cmd[0] + " is not a valid nonnegative integer.\n";
            State = 1;
            return;
        }
        if (argc <= cnt + 1)//若变量个数 <= 左移次数, 则直接把argc置1即可
            argc = 1;
        else
            argc -= cnt;
        //进行左移
        for (int i = 1; i < argc; i++)
            argv[i] = argv[i + cnt];
        Message1 = "";
        Message2 = "";
        State = 0;
    }
}

//test命令
void _test(string cmd[], int ParaNum){
    if (ParaNum <= 1){
        Message1 = "";
        Message2 = "test: Too few parameters.\n";
        State = 1;
        return;
    }
    if (ParaNum >= 4){
        Message1 = "";
        Message2 = "test: Too many parameters.\n";
        State = 1;
        return;
    }
    if (ParaNum == 2){//一元运算符，有且仅有2个参数
        string ValueStr = GetValue(cmd[1]);
        State = 0; Message1 = Message2 = "";
        if (cmd[0] == "-e"){//文件存在
            struct stat buf;
            int ret = lstat(ValueStr.c_str(), &buf);
            if (ret == 0) Message1 = "true\n";
            else Message1 = "false\n";
        }else if (cmd[0] == "-r"){//文件可读
            struct stat buf;
            int ret = lstat(ValueStr.c_str(), &buf);
            if (ret == 0 && access(ValueStr.c_str(), R_OK)) Message1 = "true\n";
            else Message1 = "false\n";
        }else if (cmd[0] == "-w"){//文件可写
            struct stat buf;
            int ret = lstat(ValueStr.c_str(), &buf);
            if (ret == 0 && access(ValueStr.c_str(), W_OK)) Message1 = "true\n";
            else Message1 = "false\n";
        }else if (cmd[0] == "-x"){//文件可执行
            struct stat buf;
            int ret = lstat(ValueStr.c_str(), &buf);
            if (ret == 0 && access(ValueStr.c_str(), X_OK)) Message1 = "true\n";
            else Message1 = "false\n";
        }else if (cmd[0] == "-s"){//文件至少有一个字符
            struct stat buf;
            int ret = lstat(ValueStr.c_str(), &buf);
            if (ret == 0 && buf.st_size) Message1 = "true\n";
            else Message1 = "false\n";
        }else if (cmd[0] == "-d"){//文件为目录
            struct stat buf;
            int ret = lstat(ValueStr.c_str(), &buf);
            if (ret == 0 && S_ISDIR(buf.st_mode)) Message1 = "true\n";
            else Message1 = "false\n";
        }else if (cmd[0] == "-f"){//文件为普通文件
            struct stat buf;
            int ret = lstat(ValueStr.c_str(), &buf);
            if (ret == 0 && S_ISREG(buf.st_mode)) Message1 = "true\n";
            else Message1 = "false\n";
        }else if (cmd[0] == "-c"){//文件为字符型特殊文件
            struct stat buf;
            int ret = lstat(ValueStr.c_str(), &buf);
            if (ret == 0 && S_ISCHR(buf.st_mode)) Message1 = "true\n";
            else Message1 = "false\n";
        }else if (cmd[0] == "-b"){//文件为块特殊文件
            struct stat buf;
            int ret = lstat(ValueStr.c_str(), &buf);
            if (ret == 0 && S_ISBLK(buf.st_mode)) Message1 = "true\n";
            else Message1 = "false\n";
        }else if (cmd[0] == "-h" || cmd[0] == "-L"){//文件为符号链接
            struct stat buf;
            int ret = lstat(ValueStr.c_str(), &buf);
            if (ret == 0 && S_ISLNK(buf.st_mode)) Message1 = "true\n";
            else Message1 = "false\n";
        }else if (cmd[0] == "-p"){//文件为命名管道
            struct stat buf;
            int ret = lstat(ValueStr.c_str(), &buf);
            if (ret == 0 && S_ISFIFO(buf.st_mode)) Message1 = "true\n";
            else Message1 = "false\n";
        }else if (cmd[0] == "-S"){//文件为嵌套字
            struct stat buf;
            int ret = lstat(ValueStr.c_str(), &buf);
            if (ret == 0 && S_ISSOCK(buf.st_mode)) Message1 = "true\n";
            else Message1 = "false\n";
        }else if (cmd[0] == "-G"){//文件被实际组拥有
            struct stat buf;
            int ret = lstat(ValueStr.c_str(), &buf);
            if (ret == 0 && buf.st_gid == getgid()) Message1 = "true\n";
            else Message1 = "false\n";
        }else if (cmd[0] == "-O"){//文件被实际用户拥有
            struct stat buf;
            int ret = lstat(ValueStr.c_str(), &buf);
            if (ret == 0 && buf.st_uid == getuid()) Message1 = "true\n";
            else Message1 = "false\n";
        }else if (cmd[0] == "-g"){//文件有设置组位
            struct stat buf;
            int ret = lstat(ValueStr.c_str(), &buf);
            if (ret == 0 && (S_ISGID & buf.st_mode)) Message1 = "true\n";
            else Message1 = "false\n";
        }else if (cmd[0] == "-u"){//文件有设置用户位
            struct stat buf;
            int ret = lstat(ValueStr.c_str(), &buf);
            if (ret == 0 && (S_ISUID & buf.st_mode)) Message1 = "true\n";
            else Message1 = "false\n";
        }else if (cmd[0] == "-k"){//文件有设置粘滞位
            struct stat buf;
            int ret = lstat(ValueStr.c_str(), &buf);
            if (ret == 0 && (S_ISVTX & buf.st_mode)) Message1 = "true\n";
            else Message1 = "false\n";
        }else if (cmd[0] == "-n"){//字符串长度非0
            if (ValueStr.length()) Message1 = "true\n";
            else Message1 = "false\n";
        }else if (cmd[0] == "-z"){//字符串长度为0
            if (ValueStr.length() == 0) Message1 = "true\n";
            else Message1 = "false\n";
        }else{
            State = 1;
            Message2 = "test: Unknown command " + cmd[0] + ".\n";
        }
    }
    if (ParaNum == 3){//二元运算符，有且仅有3个参数
        string ValueStr1 = GetValue(cmd[0]);
        string ValueStr2 = GetValue(cmd[2]);
        State = 0; Message1 = Message2 = "";
        if (cmd[1] == "-ef"){//文件1和文件2的设备和inode相同
            struct stat buf1, buf2;
            int ret1 = lstat(ValueStr1.c_str(), &buf1);
            int ret2 = lstat(ValueStr2.c_str(), &buf2);
            if (ret1 == 0 && ret2 == 0 && buf1.st_dev == buf2.st_dev && buf1.st_ino == buf2.st_ino) Message1 = "true\n";
            else Message1 = "false\n";
        }else if (cmd[1] == "-nt"){//文件1比文件2更新
            struct stat buf1, buf2;
            int ret1 = lstat(ValueStr1.c_str(), &buf1);
            int ret2 = lstat(ValueStr2.c_str(), &buf2);
            if (ret1 == 0 && ret2 == 0 && TimeCMP(buf1.st_mtim, buf2.st_mtim) == 1) Message1 = "true\n";
            else Message1 = "false\n";
        }else if (cmd[1] == "-ot"){//文件1比文件2更旧
            struct stat buf1, buf2;
            int ret1 = lstat(ValueStr1.c_str(), &buf1);
            int ret2 = lstat(ValueStr2.c_str(), &buf2);
            if (ret1 == 0 && ret2 == 0 && TimeCMP(buf1.st_mtim, buf2.st_mtim) == -1) Message1 = "true\n";
            else Message1 = "false\n";
        }else if (cmd[1] == "="){//字符串相等
            if (ValueStr1 == ValueStr2) Message1 = "true\n";
            else Message1 = "false\n";
        }else if (cmd[1] == "!="){//字符串不等
            if (ValueStr1 != ValueStr2) Message1 = "true\n";
            else Message1 = "false\n";
        }else if (cmd[1] == "-eq"){//整数==
            int Value1, Value2;
            try{
                Value1 = StringToInt(ValueStr1);
            }catch(...){
                State = 1;
                Message2 = "test: " + ValueStr1 + " is not a valid integer.\n";
                return;
            }
            try{
                Value2 = StringToInt(ValueStr2);
            }catch(...){
                State = 1;
                Message2 = "test: " + ValueStr2 + " is not a valid integer.\n";
                return;
            }
            if (Value1 == Value2) Message1 = "true\n";
            else Message1 = "false\n";
        }else if (cmd[1] == "-ge"){//整数>=
            int Value1, Value2;
            try{
                Value1 = StringToInt(ValueStr1);
            }catch(...){
                State = 1;
                Message2 = "test: " + ValueStr1 + " is not a valid integer.\n";
                return;
            }
            try{
                Value2 = StringToInt(ValueStr2);
            }catch(...){
                State = 1;
                Message2 = "test: " + ValueStr2 + " is not a valid integer.\n";
                return;
            }
            if (Value1 >= Value2) Message1 = "true\n";
            else Message1 = "false\n";
        }else if (cmd[1] == "-gt"){//整数>
            int Value1, Value2;
            try{
                Value1 = StringToInt(ValueStr1);
            }catch(...){
                State = 1;
                Message2 = "test: " + ValueStr1 + " is not a valid integer.\n";
                return;
            }
            try{
                Value2 = StringToInt(ValueStr2);
            }catch(...){
                State = 1;
                Message2 = "test: " + ValueStr2 + " is not a valid integer.\n";
                return;
            }
            if (Value1 > Value2) Message1 = "true\n";
            else Message1 = "false\n";
        }else if (cmd[1] == "-le"){//整数<=
            int Value1, Value2;
            try{
                Value1 = StringToInt(ValueStr1);
            }catch(...){
                State = 1;
                Message2 = "test: " + ValueStr1 + " is not a valid integer.\n";
                return;
            }
            try{
                Value2 = StringToInt(ValueStr2);
            }catch(...){
                State = 1;
                Message2 = "test: " + ValueStr2 + " is not a valid integer.\n";
                return;
            }
            if (Value1 <= Value2) Message1 = "true\n";
            else Message1 = "false\n";
        }else if (cmd[1] == "-lt"){//整数<
            int Value1, Value2;
            try{
                Value1 = StringToInt(ValueStr1);
            }catch(...){
                State = 1;
                Message2 = "test: " + ValueStr1 + " is not a valid integer.\n";
                return;
            }
            try{
                Value2 = StringToInt(ValueStr2);
            }catch(...){
                State = 1;
                Message2 = "test: " + ValueStr2 + " is not a valid integer.\n";
                return;
            }
            if (Value1 < Value2) Message1 = "true\n";
            else Message1 = "false\n";
        }else if (cmd[1] == "-ne"){//整数!=
            int Value1, Value2;
            try{
                Value1 = StringToInt(ValueStr1);
            }catch(...){
                State = 1;
                Message2 = "test: " + ValueStr1 + " is not a valid integer.\n";
                return;
            }
            try{
                Value2 = StringToInt(ValueStr2);
            }catch(...){
                State = 1;
                Message2 = "test: " + ValueStr2 + " is not a valid integer.\n";
                return;
            }
            if (Value1 != Value2) Message1 = "true\n";
            else Message1 = "false\n";
        }else{//无法识别的运算符
            State = 1;
            Message2 = "test: Unknown command " + cmd[0] + ".\n";
        }
    }
}

//time列出当前时间
void _time(string cmd[], int ParaNum){
    if (ParaNum > 0){//参数过多，报错
        Message1 = "";
        Message2 = "time: Too many parameters.\n";
        State = 1;
        return;
    }
    //得到当前时间
    time_t tt = time(NULL);
    struct tm * t = localtime(&tt);
    //用stringstream生成返回信息
    stringstream sstm;
    const char * Week[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    sstm << t->tm_year + 1900 << "." << t->tm_mon + 1 << "." << t->tm_mday << "."
         << " " << Week[t->tm_wday]
         << " " << t->tm_hour << ":" << t->tm_min << ":" << t->tm_sec;
    getline(sstm, Message1);
    Message1 += "\n";
    Message2 = "";
    State = 0;
}

//输出umask或更改umask的值
void _umask(string cmd[], int ParaNum){
    if (ParaNum > 1){//参数过多，报错
        Message1 = "";
        Message2 = "umask: Too many parameters.\n";
        State = 1;
    } else if (ParaNum == 1){//只有一个参数，表示设置umask的值
        if (cmd[0].length() >= 5){//参数多于4位，报错
            Message1 = "";
            Message2 = "umask: Expected at most 4 octonary digits: " + cmd[0] + "\n";
            State = 1;
        }else{
            while (cmd[0].length() < 4) cmd[0] = "0" + cmd[0];//补齐到恰好4位
            if (cmd[0][0] < '0' || cmd[0][0] > '7'){//判断是否为8进制数
                Message1 = "";
                Message2 = to_string(cmd[0][0] - '0') + " is not an octonary digit.\n";
                State = 1;
            }else if (cmd[0][1] < '0' || cmd[0][1] > '7'){//判断是否为8进制数
                Message1 = "";
                Message2 = to_string(cmd[0][1] - '0') + " is not an octonary digit.\n";
                State = 1;
            }else if (cmd[0][2] < '0' || cmd[0][2] > '7'){//判断是否为8进制数
                Message1 = "";
                Message2 = to_string(cmd[0][2] - '0') + " is not an octonary digit.\n";
                State = 1;
            }else if (cmd[0][3] < '0' || cmd[0][3] > '7'){//判断是否为8进制数
                Message1 = "";
                Message2 = to_string(cmd[0][3] - '0') + " is not an octonary digit.\n";
                State = 1;
            }else{
                int newmode = ((cmd[0][0] - '0') << 9) | ((cmd[0][1] - '0') << 6) | ((cmd[0][2] - '0') << 3) | (cmd[0][3] - '0');
                umask(newmode);
                Message1 = "";
                Message2 = "";
                State = 0;
            }
        }
    }else{//无参，表示显示umask的值
        mode_t currentmode = umask(0);
        umask(currentmode);
        Message1 = to_string((currentmode >> 9) & 7) + to_string((currentmode >> 6) & 7)
                 + to_string((currentmode >> 3) & 7) + to_string(currentmode & 7) + "\n";
        Message2 = "";
        State = 0;
    }
}

//unset命令删除环境变量
void _unset(string cmd[], int ParaNum){
    if (ParaNum > 1){
        Message1 = "";
        Message2 = "unset: Too many parameters.\n";
        State = 1;
    } else if (ParaNum == 0){
        Message1 = "";
        Message2 = "unset: Input a variable's name.\n";
        State = 1;
    } else {
        Message1 = "";
        Message2 = "";
        State = 0;
        unsetenv(cmd[0].c_str());
    }
}


//第三层解析指令：执行单条指令，可能包含重定向
void ExecSingleCMD(string cmd[], int ParaNum, bool WithFork){
    //备份三个标准输入输出
    int InputFD = dup(STDIN_FILENO), OutputFD = dup(STDOUT_FILENO), ErrorFD = dup(STDERR_FILENO);
    int InputFDNew = -1, OutputFDNew = -1, ErrorFDNew = -1;
    //搜索重定向符号
    State = 0;
    string InputFile = "", OutputFile = "", ErrorFile = "";
    for (int i = ParaNum - 2; i >= 0; i--) {//注意，i从ParaNum-2开始枚举，因为重定向符号不可能是最后一个字符串
        //输入重定向
        if (cmd[i] == "<" || cmd[i] == "0<"){
            if (InputFile != ""){
                Message1 = "";
                Message2 = "MyShell: Expected at most 1 input redirection.\n";
                State = 1;
                break;
            }
            InputFile = cmd[i + 1];
            InputFDNew = open(InputFile.c_str(), O_RDONLY);
            if(InputFDNew < 0){
                Message1 = "";
                Message2 = "MyShell: Unable to open " + InputFile + ".\n";
                State = 1;
                break;
            }
            dup2(InputFDNew, STDIN_FILENO);
            close(InputFDNew);
            ParaNum = i;
        }
        //输出重定向，覆盖模式
        else if (cmd[i] == ">" || cmd[i] == "1>"){
            if (OutputFile != ""){
                Message1 = "";
                Message2 = "MyShell: Expected at most 1 output redirection.\n";
                State = 1;
                break;
            }
            OutputFile = cmd[i + 1];
            OutputFDNew = open(OutputFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
            if(OutputFDNew < 0){
                Message1 = "";
                Message2 = "MyShell: Unable to open " + OutputFile + ".\n";
                State = 1;
                break;
            }
            dup2(OutputFDNew, STDOUT_FILENO);
            close(OutputFDNew);
            ParaNum = i;
        }
        //输出重定向，追加模式
        else if (cmd[i] == ">>" || cmd[i] == "1>>"){
            if (OutputFile != ""){
                Message1 = "";
                Message2 = "MyShell: Expected at most 1 output redirection.\n";
                State = 1;
                break;
            }
            OutputFile = cmd[i + 1];
            OutputFDNew = open(OutputFile.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0666);
            if(OutputFDNew < 0){
                Message1 = "";
                Message2 = "MyShell: Unable to open " + OutputFile + ".\n";
                State = 1;
                break;
            }
            dup2(OutputFDNew, STDOUT_FILENO);
            close(OutputFDNew);
            ParaNum = i;
        }
        //错误重定向，覆盖模式
        else if (cmd[i] == "2>"){
            if (ErrorFile != ""){
                Message1 = "";
                Message2 = "MyShell: Expected at most 1 error redirection.\n";
                State = 1;
                break;
            }
            ErrorFile = cmd[i + 1];
            ErrorFDNew = open(ErrorFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
            if(ErrorFDNew < 0){
                Message1 = "";
                Message2 = "MyShell: Unable to open " + ErrorFile + ".\n";
                State = 1;
                break;
            }
            dup2(ErrorFDNew, STDERR_FILENO);
            close(ErrorFDNew);
            ParaNum = i;
        }
        //错误重定向，追加模式
        else if (cmd[i] == "2>>"){
            if (ErrorFile != ""){
                Message1 = "";
                Message2 = "MyShell: Expected at most 1 error redirection.\n";
                State = 1;
                break;
            }
            ErrorFile = cmd[i + 1];
            ErrorFDNew = open(ErrorFile.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0666);
            if(ErrorFDNew < 0){
                Message1 = "";
                Message2 = "MyShell: Unable to open " + ErrorFile + ".\n";
                State = 1;
                break;
            }
            dup2(ErrorFDNew, STDERR_FILENO);
            close(ErrorFDNew);
            ParaNum = i;
        }
    }
    if (State == 0){//如果前面的重定向处理没有出错，则继续
        //解析指令
        if (ParaNum == 0 || cmd[0][0] == '#') {
            Message1 = "";
            Message2 = "";
            State = 0;
        }else if (cmd[0] == "bg") {
            _bg(cmd + 1, ParaNum - 1);
        }else if (cmd[0] == "cd") {
            _cd(cmd + 1, ParaNum - 1);
        }else if (cmd[0] == "clr") {
            _clr(cmd + 1, ParaNum - 1);
        }else if (cmd[0] == "dir") {
            _dir(cmd + 1, ParaNum - 1);
        }else if (cmd[0] == "echo") {
            _echo(cmd + 1, ParaNum - 1);
        }else if (cmd[0] == "exec") {
            _exec(cmd + 1, ParaNum - 1);
        }else if (cmd[0] == "exit") {
            _exit(cmd + 1, ParaNum - 1);
        }else if (cmd[0] == "fg") {
            _fg(cmd + 1, ParaNum - 1);
        }else if (cmd[0] == "help") {
            _help(cmd + 1, ParaNum - 1);
        }else if (cmd[0] == "jobs") {
            _jobs(cmd + 1, ParaNum - 1);
        }else if (cmd[0] == "pwd") {
            _pwd(cmd + 1, ParaNum - 1);
        }else if (cmd[0] == "set") {
            _set(cmd + 1, ParaNum - 1);
        }else if (cmd[0] == "shift") {
            _shift(cmd + 1, ParaNum - 1);
        }else if (cmd[0] == "test") {
            _test(cmd + 1, ParaNum - 1);
        }else if (cmd[0] == "time") {
            _time(cmd + 1, ParaNum - 1);
        }else if (cmd[0] == "umask") {
            _umask(cmd + 1, ParaNum - 1);
        }else if (cmd[0] == "unset") {
            _unset(cmd + 1, ParaNum - 1);
        }else if (cmd[0] == "exit") {
            _exit(cmd + 1, ParaNum - 1);
        }else{//其他命令，表示程序调用
            Message1 = "";
            Message2 = "";
            State = 0;
            if (WithFork){
                //fork将父进程拷贝一份变成子进程
                SubPID = fork();
                if (SubPID == 0){//子进程
                    //设置PARENT环境变量
                    setenv("PARENT", ShellPath.c_str(), 1);
                    _exec(cmd, ParaNum);
                    //如果能执行到这一步，说明exec出错
                    Message2 = "MyShell: Unable to execute " + cmd[0] + ".\n";
                    write(STDERR_FILENO, Message2.c_str(), Message2.length());
                    exit(0);
                }
                //父进程等待子进程完成
                while (SubPID != -1 && !waitpid(SubPID, NULL, WNOHANG));
                SubPID = -1;
            }else{
                //设置PARENT环境变量
                setenv("PARENT", ShellPath.c_str(), 1);
                _exec(cmd, ParaNum);
                //如果能执行到这一步，说明exec出错
                Message2 = "MyShell: Unable to execute " + cmd[0] + ".\n";
            }
        }
    }
    //输出结果
    write(STDOUT_FILENO, Message1.c_str(), Message1.length());
    write(STDERR_FILENO, Message2.c_str(), Message2.length());
    //恢复三个标准输入输出
    dup2(InputFD, STDIN_FILENO); dup2(OutputFD, STDOUT_FILENO); dup2(ErrorFD, STDERR_FILENO);
    close(InputFD); close(OutputFD); close(ErrorFD);
}

//第二层解析指令：执行多条用管道符分隔的指令。单条指令交给ExecSingleCMD函数
void ExecMultipCMD(string cmd[], int ParaNum, bool WithFork){
    //如果整个指令串都没有管道符，则直接在当前进程中执行
    bool PipeFlag = false;
    for (int i = 0; i < ParaNum; i++)
        if (cmd[i] == "|"){
            PipeFlag = true;
            break;
        }
    if (PipeFlag == false){
        ExecSingleCMD(cmd, ParaNum, WithFork);
        return;
    }
    //否则，生成一个子进程，执行用管道符连接的多条指令
    if (WithFork) SubPID = fork();
    if (WithFork && SubPID){//父进程
        while (SubPID != -1 && !waitpid(SubPID, NULL, WNOHANG));
        SubPID = -1;
    }else{//子进程
        //将信号处理函数恢复至系统默认
        signal(SIGINT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        //标记上一个管道符的位置
        int LastPipe = -1;
        //管道的文件描述符，只需要记录两个管道即可（但整个过程可能会创建很多次管道）
        int FirstFD[2], SecondFD[2];
        //执行管道符指令时，所有子进程的pid. 之所以要存储该信息，是为了在解析指令结束后使用waitpid等待所有子进程完成
        const int MaxPipe = 1024;
        int PipePid[MaxPipe];
        int PipeCNT;
        //在末尾临时添加一个管道符
        cmd[ParaNum++] = "|";
        //扫描，找出所有的管道符
        PipeCNT = 0;
        for (int i = 0; i < ParaNum; i++)
            if (cmd[i] == "|"){//遇到管道符，把LastPipe + 1到i - 1之间的指令提取出来，作为子进程执行
                //创建管道
                if (LastPipe == -1) {//第一个遇到的管道符
                    FirstFD[0] = STDIN_FILENO;
                    FirstFD[1] = -1;
                    pipe(SecondFD);
                }else if (i == ParaNum - 1){//最后一个管道符
                    if (FirstFD[0] != STDIN_FILENO) close(FirstFD[0]);//防止误关STDIN
                    FirstFD[0] = SecondFD[0]; FirstFD[1] = SecondFD[1]; close(FirstFD[1]);
                    SecondFD[0] = -1; SecondFD[1] = STDOUT_FILENO;
                }else{//既不是第一个管道符也不是最后一个管道符
                    if (FirstFD[0] != STDIN_FILENO) close(FirstFD[0]);//防止误关STDIN
                    FirstFD[0] = SecondFD[0]; FirstFD[1] = SecondFD[1]; close(FirstFD[1]);
                    pipe(SecondFD);
                }
                //拷贝进程
                PipePid[PipeCNT++] = fork();
                if (PipePid[PipeCNT - 1] == 0){  //子进程
                    //设置信号处理函数
                    signal(SIGINT, SIG_IGN);
                    signal(SIGTSTP, SIG_DFL);
                    //重定向
                    dup2(FirstFD[0], STDIN_FILENO);
                    dup2(SecondFD[1], STDOUT_FILENO);
                    close(FirstFD[1]); close(SecondFD[0]);
                    //执行指令
                    ExecSingleCMD(cmd + LastPipe + 1, i - LastPipe - 1, false);
                    exit(0);
                }
                LastPipe = i;
            }
        close(FirstFD[0]);
        //等待所有子进程完成
        for (int i = 0; i < PipeCNT; i++)
            waitpid(PipePid[i], NULL, 0);
        exit(0);
    }
}

//第一层解析指令：处理"&"后台运行符号，其余交给ExecMultipCMD函数
void Exec(string CMD){
    //执行之前，先扫描Jobs表，输出已完成的进程，并更新Jobs表
    for (int i = Front; i < Rear; i++){
        if (States[i] && waitpid(Jobs[i], NULL, WNOHANG) == Jobs[i]) {
            if (InIsTerminal){//只有输入来自终端的时候才要打印子进程表
                string str = JobString(i, 1);
                write(TerminalOut, str.c_str(), str.length());//注意：子进程表只输出到终端，不能输出到STDOUT！！！！！
            }
            States[i] = 0;
            if (Front == i) Front++;
        }
    }
    if (Front == Rear) Front = Rear = 0;
    //利用stringstream切割字符串
    stringstream sstm;
    int ParaNum = 0;//用空白字符分隔的参数个数
    string cmd[1024];//切割结果
    sstm << CMD;
    while(1){
        cmd[ParaNum] = "";
        sstm >> cmd[ParaNum];
        if (cmd[ParaNum] == "") break;
        ParaNum++;
    }
    //此时，指令串分隔完毕。检查末尾是否是&，表示后台运行
    if (ParaNum > 0 && cmd[ParaNum - 1] == "&") {
        ParaNum--;
        int pid = fork();
        if (pid){
            //父进程，存储子进程的id，指令等信息
            Jobs[Rear] = pid;
            States[Rear] = 1;
            CMDInfo[Rear] = "";
            for (int i = 0; i < ParaNum; i++) CMDInfo[Rear] += cmd[i] + " ";
            CurrentCMD = CMDInfo[Rear];
            Rear++;
            if (InIsTerminal){//只有输入来自终端的时候才要打印子进程表
                string str = JobString(Rear - 1);
                write(TerminalOut, str.c_str(), str.length());//注意：子进程表只输出到终端，不能输出到STDOUT！！！！！
            }
        }else{//子进程，执行指令
            setpgid(0, 0);//使子进程单独成为一个进程组。后台进程组自动忽略Ctrl+Z、Ctrl+C等信号
            ExecMultipCMD(cmd, ParaNum, false);
            exit(0);
        }
    }else{//没有&符号，前台运行
        CurrentCMD = CMD;
        ExecMultipCMD(cmd, ParaNum);
    }
}

//信号处理函数，用于处理Ctrl+C、Ctrl+Z等组合键
void SignalProcess(int Signal){
    switch (Signal){
        case SIGINT: //Ctrl+C, 终止当前进程
            write(TerminalOut, "\n", 1);
            //什么都不需要做，因为子进程也会接收到SIGINT信号，子进程被终止
            break;
        case SIGTSTP: //Ctrl+Z，挂起当前进程
            write(TerminalOut, "\n", 1);
            if (SubPID != -1){
                setpgid(SubPID, 0);
                kill(SubPID, SIGTSTP);
                Jobs[Rear] = SubPID;
                States[Rear] = 2;
                CMDInfo[Rear] = CurrentCMD;
                Rear++;
                if (InIsTerminal){//只有输入来自终端的时候才要打印子进程表
                    string str = JobString(Rear - 1);
                    write(TerminalOut, str.c_str(), str.length());//注意：子进程表只输出到终端，不能输出到STDOUT！！！！！
                }
                SubPID = -1;
            }
            break;
        case SIGCONT: //继续执行任务信号
            break;
    }
}

//初始化
void Init(int Argc, char * Argv[]){
    char buffer[1024] = {0};
    //把参数赋值给全局变量
    argc = Argc;
    for (int i = 0; i < argc; i++) argv[i] = Argv[i];
    //若超过两个参数, 则把标准输入重定向到argv[1]
    int InputFD = -1;
    if (argc >= 2){
        InputFD = open(argv[1].c_str(), O_RDONLY);
        if (InputFD < 0){
            sprintf(buffer, "MyShell: Unable to open %s.\n", argv[1].c_str());
            write(STDERR_FILENO, buffer, 1024);
            _exit(0);
        }
        dup2(InputFD, STDIN_FILENO);
        close(InputFD);
    }
    //得到进程号
    PID = getpid();
    SubPID = -1;
    //得到当前主机名
    gethostname(buffer, 1024);
    HostName = buffer;
    //得到当前用户名
    UserName = getenv("USERNAME");
    //获取主目录地址
    HomeDir = getenv("HOME");
    //获取当前地址
    PWD = getenv("PWD");
    //帮助手册路径
    HelpPath = PWD + "/help";
    //获取程序自身的路径
    int len = readlink("/proc/self/exe", buffer, 1024);
    buffer[len] = '\0';
    ShellPath = buffer;
    setenv("SHELL", buffer, 1);
    //设置父进程的路径
    setenv("PARENT", "\\bin\\bash", 1);
    //初始化后台进程表
    Front = Rear = 0;
    //设置终端标准输入输出的文件描述符
    TerminalIn  = open("/dev/tty", O_RDONLY);
    TerminalOut = open("/dev/tty", O_WRONLY);
    //判断标准输入输出是否来自终端
    struct stat FileInfo;
    fstat(STDIN_FILENO,  &FileInfo);
    InIsTerminal  = S_ISCHR(FileInfo.st_mode);
    fstat(STDOUT_FILENO, &FileInfo);
    OutIsTerminal = S_ISCHR(FileInfo.st_mode);
    //设置信号处理函数
    if (InIsTerminal){//只有输入来自终端时才对Ctrl+C、Ctrl+Z等快捷键进行作业控制
        signal(SIGINT, SignalProcess);
        signal(SIGTSTP, SignalProcess);
    }
}
