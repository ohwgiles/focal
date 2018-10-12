# focal [![status](https://ci.ohwg.net/badge/focal.svg)](https://ci.ohwg.net/jobs/focal)

![screenshot](https://user-images.githubusercontent.com/1444499/45927628-c5ab1000-bf3e-11e8-9f70-1685f96b0b08.png)

Focal is a desktop calendar application for Linux.

Focal intends to be a powerful but lightweight calendar suitable for busy work environments. This means 1st-class support for remotely hosted calendars (CalDAV), a complete week view, and advanced scheduling features.

Focal is implemented in C and depends only on GTK, libical, libcurl, libxml2, json-glib, and libsecret.

Focal is in very early stages of development and needs a lot more work before it will be useful. You can help! All contributions are welcome, including those from beginner developers. Please see [CONTRIBUTING.md](CONTRIBUTING.md) for more information on how to get involved.

### Building from source

```
# Install dependencies
sudo apt-get install build-essential git cmake libgtk-3-dev libxml2-dev libical-dev libcurl4-gnutls-dev libjson-glib-dev libsecret-1-dev
# Clone the sources
git clone git@github.com:ohwgiles/focal.git
# Create a build directory and generate Makefiles
mkdir focal-build && cd focal-build && cmake ../focal
# Build and run focal
make && ./focal
# The external authentication for Google Calendar requires an installed copy of focal:
sudo make install && sudo update-desktop-database
# Alternatively, you can copy res/focal.desktop to ~/.local/share/applications, modify
# it to set the correct path to the focal executable, and run update-desktop-database
```

### CalDAV servers

This section describes how to run common CalDAV servers in docker and connect to them with focal.

#### [Nextcloud (SabreDAV)](https://nextcloud.com/):

```
docker run --name focal-test-nextcloud -d --rm -p 7772:80 nextcloud
docker exec -it focal-test-nextcloud runuser -u www-data -- /var/www/html/occ maintenance:install --admin-pass=admin
# focal.conf snippet
[nextcloud]
type=caldav
url=http://localhost:7772/remote.php/dav/calendars/admin/personal/
user=admin
pass=admin
```

#### [Apple Calendar and Contacts Server](https://www.calendarserver.org/):

```
docker run --name focal-test-ccs -d --rm -p 7771:8008 $(docker build -qf - . <<-EOF
  FROM pluies/ccs-calendarserver
  RUN bash -c "cd /opt/ccs && curl -L https://github.com/apple/ccs-calendarserver/archive/CalendarServer-9.2.tar.gz | tar xz --strip-components=1 ccs-calendarserver-CalendarServer-9.2/conf && cp /opt/ccs/conf/caldavd-{test,dev}.plist"
EOF
)
# focal.conf snippet
[ccs]
type=caldav
url=http://localhost:7771/calendars/users/admin/calendar/
user=admin
pass=admin
```

#### [Radicale](https://radicale.org/):

```
docker run --name focal-test-radicale -d --rm -p 7773:5232 tomsquest/docker-radicale
curl localhost:7773/admin/personal -X MKCOL -d '<?xml version="1.0" encoding="UTF-8" ?><mkcol xmlns="DAV:" xmlns:C="urn:ietf:params:xml:ns:caldav" xmlns:CR="urn:ietf:params:xml:ns:carddav" xmlns:I="http://apple.com/ns/ical/" xmlns:INF="http://inf-it.com/ns/ab/"><set><prop><resourcetype><collection /><C:calendar /></resourcetype><C:supported-calendar-component-set><C:comp name="VEVENT" /></C:supported-calendar-component-set><displayname>Personal</displayname><C:calendar-description>i</C:calendar-description></prop></set></mkcol>'
# focal.conf snippet
[radicale]
type=caldav
url=http://localhost:7773/admin/personal/
user=admin
pass=admin
```

