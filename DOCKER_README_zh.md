# Docker

## 重要说明

从 v0.18.0 开始，标签名称已更改。您可能不再能够使用 `latest`, `master`, `vX.X.X`。

## 构建您自己的容器

此镜像为您在自己的 Docker 项目中轻松使用最新的 Sunshine 版本提供了一种方法。目前它不打算作为独立容器使用，应被视为实验性的。

```dockerfile
ARG SUNSHINE_VERSION=latest
ARG SUNSHINE_OS=ubuntu-22.04
FROM lizardbyte/sunshine:${SUNSHINE_VERSION}-${SUNSHINE_OS}

# 安装 Steam, Wayland 等。

ENTRYPOINT steam && sunshine
```

### SUNSHINE_VERSION

- `latest`, `master`, `vX.X.X`
- commit hash (提交哈希)

### SUNSHINE_OS

Sunshine 镜像提供以下标签后缀，基于它们各自的基础镜像。

- `debian-bookworm`
- `ubuntu-22.04`
- `ubuntu-24.04`

### 标签 (Tags)

您必须结合 `SUNSHINE_VERSION` 和 `SUNSHINE_OS` 来确定要拉取的标签。格式应为
`<SUNSHINE_VERSION>-<SUNSHINE_OS>`。例如，`latest-ubuntu-24.04`。

查看我们在 [Docker Hub](https://hub.docker.com/r/lizardbyte/sunshine/tags) 或
[GHCR](https://github.com/LizardByte/Sunshine/pkgs/container/sunshine/versions) 上所有可用的标签以获取更多信息。

## 使用案例

这是使用 Sunshine 的 Docker 项目列表。如果有遗漏？请告诉我们！

- [Games on Whales](https://games-on-whales.github.io)

## 端口和挂载卷映射

以下是所需映射的示例。配置文件将保存到容器中的 `/config`。

### 使用 docker run

创建并运行容器（替换您的 `<values>` 值）：

```bash
docker run -d \
  --device /dev/dri/ \
  --name=<image_name> \
  --restart=unless-stopped \
  --ipc=host \
  -e PUID=<uid> \
  -e PGID=<gid> \
  -e TZ=<timezone> \
  -v <path to data>:/config \
  -p 47984-47990:47984-47990/tcp \
  -p 48010:48010 \
  -p 47998-48000:47998-48000/udp \
  <image>
```

### 使用 docker-compose

创建一个包含以下内容的 `docker-compose.yml` 文件（替换您的 `<values>` 值）：

```yaml
version: "3"
services:
  <image_name>:
    image: <image>
    container_name: sunshine
    restart: unless-stopped
    volumes:
      - <path to data>:/config
    environment:
      - PUID=<uid>
      - PGID=<gid>
      - TZ=<timezone>
    ipc: host
    ports:
      - "47984-47990:47984-47990/tcp"
      - "48010:48010"
      - "47998-48000:47998-48000/udp"
```

### 使用 podman run

创建并运行容器（替换您的 `<values>` 值）：

```bash
podman run -d \
  --device /dev/dri/ \
  --name=<image_name> \
  --restart=unless-stopped \
  --userns=keep-id \
  -e PUID=<uid> \
  -e PGID=<gid> \
  -e TZ=<timezone> \
  -v <path to data>:/config \
  -p 47984-47990:47984-47990/tcp \
  -p 48010:48010 \
  -p 47998-48000:47998-48000/udp \
  <image>
```

### 参数

您必须使用自己的设置替换 `<values>`。

参数由冒号分隔成两半。左侧代表主机，右侧代表容器。

**示例:** `-p 外部端口:内部端口` - 这显示了从容器内部到外部的端口映射。
因此 `-p 47990:47990` 将公开容器内部的 `47990` 端口，以便从主机的 IP 上的 `47990` 端口进行访问（例如 `http://<host_ip>:47990`）。内部端口必须是 `47990`，但外部端口可以更改（例如 `-p 8080:47990`）。`docker run` 和 `docker-compose` 示例中列出的所有端口都是必需的。

| 参数                        | 功能              | 示例值           | 是否必填 |
| --------------------------- | ----------------- | ---------------- | -------- |
| `-p <port>:47990`           | Web UI 端口       | `47990`          | 是       |
| `-v <path to data>:/config` | 挂载卷映射        | `/home/sunshine` | 是       |
| `-e PUID=<uid>`             | 用户 ID (User ID) | `1001`           | 否       |
| `-e PGID=<gid>`             | 组 ID (Group ID)  | `1001`           | 否       |
| `-e TZ=<timezone>`          | 查询 [TZ 值][1]   | `Asia/Shanghai`  | 否       |

如需其他配置，建议参考 _Games on Whales_ 的
[sunshine 配置](https://github.com/games-on-whales/gow/blob/2e442292d79b9d996f886b8a03d22b6eb6bddf7b/compose/streamers/sunshine.yml)。

[1]: https://en.wikipedia.org/wiki/List_of_tz_database_time_zones

#### 用户 / 组 标识符:

使用数据卷（-v 标志）时，主机操作系统与容器之间可能会出现权限问题。为了避免此问题，您可以指定用户 PUID 和组 PGID。确保主机上的数据卷目录由您指定的同一用户拥有。

在此示例中，`PUID=1001` 且 `PGID=1001`。使用如下所示的 `id 用户名` 命令查找您的 ID：

```bash
$ id dockeruser
uid=1001(dockeruser) gid=1001(dockergroup) groups=1001(dockergroup)
```

如果您想在镜像构建后更改 PUID 或 PGID，则需要重新构建镜像。

## 支持的架构

指定 `lizardbyte/sunshine:latest-<SUNSHINE_OS>` 或 `ghcr.io/lizardbyte/sunshine:latest-<SUNSHINE_OS>` 应该会为您的架构检索正确的镜像。

这些镜像支持的架构如下表所示。

| 标签后缀 (tag suffix) | amd64/x86_64 | arm64/aarch64 |
| --------------------- | ------------ | ------------- |
| debian-bookworm       | ✅           | ✅            |
| ubuntu-22.04          | ✅           | ✅            |
| ubuntu-24.04          | ✅           | ✅            |

<div class="section_buttons">

| 上一页                        |                                       下一页 |
| :---------------------------- | -------------------------------------------: |
| [更新日志](docs/changelog.md) | [第三方软件包](docs/third_party_packages.md) |

</div>

<details style="display: none;">
  <summary></summary>
  [TOC]
</details>
