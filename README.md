# focal

Focal is a desktop calendar application for Linux.

Focal intends to be a powerful but lightweight calendar suitable for busy work environments. This means 1st-class support for remotely hosted calendars (CalDAV), a complete week view, and advanced scheduling features.

Focal is implemented in C and currently depends only on GTK, libical, libcurl and libxml2.

Focal is in very early stages of development and needs a lot more work before it will be useful. You can help! All contributions are welcome, including those from beginner developers. Please see [CONTRIBUTING.md](CONTRIBUTING.md) for more information on how to get involved.

### Building from source

```
# Install dependencies
sudo apt-get install build-essential git cmake libgtk-3-dev libxml2-dev libical-dev libcurl4-gnutls-dev
# Clone the sources
git clone git@github.com:ohwgiles/focal.git
# Create a build directory and generate Makefiles
mkdir focal-build && cd focal-build && cmake ../focal
# Build and run focal
make && ./focal
```

### Running focal

So far there is no user interface for connecting to an account. You need to create the file `~/.config/focal.conf` and insert a section like this:

```
[main]
url=http://path/to/caldav/server/endpoint
user=caldav_user
pass=caldav_pass
```
