# Sunshine 基地版

基于LizardByte/Sunshine的分支，提供完整的文档支持 [Read the Docs](https://docs.qq.com/aio/DSGdQc3htbFJjSFdO?p=YTpMj5JNNdB5hEKJhhqlSB)。

## 项目简介

**Sunshine-Foundation**  is a self-hosted game stream host for Moonlight，本分支版本在原始Sunshine基础上进行了重大改进，专注于提高各种串流终端设备与windows主机接入的游戏串流体验：

### 🌟 核心特性
- **HDR友好支持** - 经过优化的HDR处理管线，提供真正的HDR游戏流媒体体验
- **集成虚拟显示器** - 内置虚拟显示器管理，无需额外软件即可创建和管理虚拟显示器
- **高级控制面板** - 直观的Web控制界面，提供实时监控和配置管理
- **低延迟传输** - 结合最新硬件能力优化的编码处理
- **智能配对** - 智能管理配对设备的对应配置文件

### 🖥️ 虚拟显示器集成
- 动态虚拟显示器创建和销毁
- 自定义分辨率和刷新率支持
- 多显示器配置管理
- 无需重启的实时配置更改


## 推荐的Moonlight客户端

建议使用以下经过优化的Moonlight客户端获得最佳的串流体验（激活套装属性）：

### 🖥️ Windows(X86_64, Arm64), MacOS, Linux 客户端
[![Moonlight-PC](https://img.shields.io/badge/Moonlight-PC-red?style=for-the-badge&logo=windows)](https://github.com/qiin2333/moonlight-qt)

### 📱 Android客户端
[![威力加强版 Moonlight-Android](https://img.shields.io/badge/威力加强版-Moonlight--Android-green?style=for-the-badge&logo=android)](https://github.com/qiin2333/moonlight-android/releases/tag/shortcut)
[![王冠版 Moonlight-Android](https://img.shields.io/badge/王冠版-Moonlight--Android-blue?style=for-the-badge&logo=android)](https://github.com/WACrown/moonlight-android)

### 📱 iOS客户端
[![真砖家版 Moonlight-iOS](https://img.shields.io/badge/真砖家版-Moonlight--iOS-lightgrey?style=for-the-badge&logo=apple)](https://github.com/TrueZhuangJia/moonlight-ios-NativeMultiTouchPassthrough)


### 🛠️ 其他资源 
[awesome-sunshine](https://github.com/LizardByte/awesome-sunshine)

## 系统要求


> [!WARNING] 
> 这些表格正在持续更新中。请不要仅基于此信息购买硬件。


<table>
    <caption id="minimum_requirements">最低配置要求</caption>
    <tr>
        <th>组件</th>
        <th>要求</th>
    </tr>
    <tr>
        <td rowspan="3">GPU</td>
        <td>AMD: VCE 1.0或更高版本，参见: <a href="https://github.com/obsproject/obs-amd-encoder/wiki/Hardware-Support">obs-amd硬件支持</a></td>
    </tr>
    <tr>
        <td>Intel: VAAPI兼容，参见: <a href="https://www.intel.com/content/www/us/en/developer/articles/technical/linuxmedia-vaapi.html">VAAPI硬件支持</a></td>
    </tr>
    <tr>
        <td>Nvidia: 支持NVENC的显卡，参见: <a href="https://developer.nvidia.com/video-encode-and-decode-gpu-support-matrix-new">nvenc支持矩阵</a></td>
    </tr>
    <tr>
        <td rowspan="2">CPU</td>
        <td>AMD: Ryzen 3或更高</td>
    </tr>
    <tr>
        <td>Intel: Core i3或更高</td>
    </tr>
    <tr>
        <td>RAM</td>
        <td>4GB或更多</td>
    </tr>
    <tr>
        <td rowspan="5">操作系统</td>
        <td>Windows: 10 22H2+ (Windows Server不支持虚拟游戏手柄)</td>
    </tr>
    <tr>
        <td>macOS: 12+</td>
    </tr>
    <tr>
        <td>Linux/Debian: 12+ (bookworm)</td>
    </tr>
    <tr>
        <td>Linux/Fedora: 39+</td>
    </tr>
    <tr>
        <td>Linux/Ubuntu: 22.04+ (jammy)</td>
    </tr>
    <tr>
        <td rowspan="2">网络</td>
        <td>主机: 5GHz, 802.11ac</td>
    </tr>
    <tr>
        <td>客户端: 5GHz, 802.11ac</td>
    </tr>
</table>

<table>
    <caption id="4k_suggestions">4K推荐配置</caption>
    <tr>
        <th>组件</th>
        <th>要求</th>
    </tr>
    <tr>
        <td rowspan="3">GPU</td>
        <td>AMD: Video Coding Engine 3.1或更高</td>
    </tr>
    <tr>
        <td>Intel: HD Graphics 510或更高</td>
    </tr>
    <tr>
        <td>Nvidia: GeForce GTX 1080或更高的具有多编码器的型号</td>
    </tr>
    <tr>
        <td rowspan="2">CPU</td>
        <td>AMD: Ryzen 5或更高</td>
    </tr>
    <tr>
        <td>Intel: Core i5或更高</td>
    </tr>
    <tr>
        <td rowspan="2">网络</td>
        <td>主机: CAT5e以太网或更好</td>
    </tr>
    <tr>
        <td>客户端: CAT5e以太网或更好</td>
    </tr>
</table>

## 技术支持

遇到问题时的解决路径：
1. 查看 [使用文档](https://docs.qq.com/aio/DSGdQc3htbFJjSFdO?p=YTpMj5JNNdB5hEKJhhqlSB)
2. 参考官方 [LizardByte文档](https://docs.lizardbyte.dev/projects/sunshine/latest/)
3. [加入QQ交流群获取帮助](https://qm.qq.com/cgi-bin/qm/qr?k=5qnkzSaLIrIaU4FvumftZH_6Hg7fUuLD&jump_from=webapi)
4. [使用两个字母！](https://uuyc.163.com/)

**问题反馈标签：**
- `hdr-support` - HDR相关问题
- `virtual-display` - 虚拟显示器问题  
- `config-help` - 配置相关问题

## 加入社区

我们欢迎大家参与讨论和贡献代码！
[![加入QQ群](https://pub.idqqimg.com/wpa/images/group.png '加入QQ群')](https://qm.qq.com/cgi-bin/qm/qr?k=WC2PSZ3Q6Hk6j8U_DG9S7522GPtItk0m&jump_from=webapi&authKey=zVDLFrS83s/0Xg3hMbkMeAqI7xoHXaM3sxZIF/u9JW7qO/D8xd0npytVBC2lOS+z)

---

**Sunshine基地版 - 让游戏串流更简单**
