# Docker

## Build your own containers
This image provides a method for you to easily use the latest Sunshine release in your own docker projects. It is not
intended to use as a standalone container at this point.

```dockerfile
FROM  lizardbyte/sunshine

# install Wayland and Steam

ENTRYPOINT start-wayland && start-steam && start-sunshine
```

## Where used
This is a list of docker projects using Sunshine. Something missing? Let us know about it!

- [Games on Whales](https://games-on-whales.github.io)

## Port and Volume mappings
Examples are below of the required mappings. The configuration file will be saved to `/config` in the container.

### Using docker run
Create and run the container (substitute your `<values>`):

```bash
docker run -d \
  --name=<image_name> \
  --restart=unless-stopped
  -v <path to data>:/config \
  -p 47984-47990:47984-47990/tcp \
  -p 48010:48010 \
  -p 47998-48000:47998-48000/udp \
  <image>
```

### Using docker-compose

Create a `docker-compose.yml` file with the following contents (substitute your `<values>`):

```yaml
version: '3'
services:
  <image_name>:
    image: <image>
    container_name: sunshine
    restart: unless-stopped
    volumes:
      - <path to data>:/config
    ports:
      - "47984-47990:47984-47990/tcp"
      - "48010:48010"
      - "47998-48000:47998-48000/udp"
```

### Parameters
You must substitute the `<values>` with your own settings.

Parameters are split into two halves separated by a colon. The left side represents the host and the right side the
container.

**Example:** `-p external:internal` - This shows the port mapping from internal to external of the container.
Therefore `-p 47990:47990` would expose port `47990` from inside the container to be accessible from the host's IP on
port `47990` (e.g. `http://<host_ip>:47990`). The internal port must be `47990`, but the external port may be changed
(e.g. `-p 8080:47990`). All the ports listed in the `docker run` and `docker-compose` examples are required.


| Parameter                   | Function                                                                             | Example Value      | Required |
|-----------------------------|--------------------------------------------------------------------------------------|--------------------|----------|
| `-p <port>:47990`           | Web UI Port                                                                          | `47990`            | True     |
| `-v <path to data>:/config` | Volume mapping                                                                       | `/home/sunshine`   | True     |
