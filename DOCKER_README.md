# Docker

## Using docker run
Create and run the container (substitute your `<values>`):

```bash
docker run -d \
  --name=sunshine \
  --restart=unless-stopped
  -v <path to data>:/config \
  -e PUID=<uid> \
  -e PGID=<gid> \
  -e TZ=<timezone> \
  -p 47990:47990 \
  -p 47984:47984 \
  -p 47989:47989 \
  -p 48010:48010 \
  -p 47998:47998 \
  -p 47999:47999 \
  -p 48000:48000 \
  -p 48002:48002 \
  -p 48010:48010 \
  sunshinestream/sunshine
```

To update the container it must be removed and recreated:

```bash
# Stop the container
docker stop sunshine
# Remove the container
docker rm sunshine
# Pull the latest update
docker pull sunshinestream/sunshine
# Run the container with the same parameters as before
docker run -d ...
```

## Using docker-compose

Create a `docker-compose.yml` file with the following contents (substitute your `<values>`):

```yaml
version: '3'
services:
  sunshine:
    image: sunshinestream/sunshine
    container_name: sunshine
    restart: unless-stopped
    volumes:
    - <path to data>:/config
    environment:
    - PUID=<uid>
    - PGID=<gid>
    - TZ=<timezone>
    ports:
    - "47990:47990"
    - "47984:47984"
    - "47989:47989"
    - "48010:48010"
    - "47998:47998"
    - "47999:47999"
    - "48000:48000"
    - "48002:48002"
    - "48010:48010"
```

Create and start the container (run the command from the same folder as your `docker-compose.yml` file):

```bash
docker-compose up -d
```

To update the container:
```bash
# Pull the latest update
docker-compose pull
# Update and restart the container
docker-compose up -d
```

## Parameters
You must substitute the `<values>` with your own settings.

Parameters are split into two halves separated by a colon. The left side represents the host and the right side the
container.

**Example:** `-p external:internal` - This shows the port mapping from internal to external of the container.
Therefore `-p 47990:47990` would expose port `47990` from inside the container to be accessible from the host's IP on
port `47990` (e.g. `http://<host_ip>:47990`). The internal port must be `47990`, but the external port may be changed
(e.g. `-p 8080:47990`).


| Parameter                   | Function             | Example Value       | Required |
| --------------------------- | -------------------- | ------------------- | -------- |
| `-p <port>:47990`           | Web UI Port          | `47990`             | True     |
| `-v <path to data>:/config` | Volume mapping       | `/home/sunshine`    | True     |
| `-e PUID=<uid>`             | User ID              | `1001`              | False    |
| `-e PGID=<gid>`             | Group ID             | `1001`              | False    |
| `-e TZ=<timezone>`          | Lookup TZ value [here](https://en.wikipedia.org/wiki/List_of_tz_database_time_zones) | `America/New_York` | True     |

### User / Group Identifiers:

When using data volumes (-v flags) permissions issues can arise between the host OS and the container. To avoid this
issue you can specify the user PUID and group PGID. Ensure the data volume directory on the host is owned by the same
user you specify.

In this instance `PUID=1001` and `PGID=1001`. To find yours use id user as below:

```bash
$ id dockeruser
uid=1001(dockeruser) gid=1001(dockergroup) groups=1001(dockergroup)
```
