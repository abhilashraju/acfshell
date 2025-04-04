SUMMARY = "acfshell: A shell environment for acf scripts"
DESCRIPTION = "Acfshell is a script environment to start, stop and track shell scripts encoded in Acf certificates"
HOMEPAGE = "https://github.com/abhilashraju/acfshell"

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=c87d289892671dc3843f34743e535818"
DEPENDS = " \
    boost \
    gtest \
    nlohmann-json \
    openssl \
    sdbusplus \
"

SRC_URI = "git://github.com/abhilashraju/acfshell.git;branch=master;protocol=https"
SRCREV = "${AUTOREV}"

S = "${WORKDIR}/git"

inherit systemd
inherit pkgconfig meson

EXTRA_OEMESON = " \
    --buildtype=minsize \
"

# Specify the source directory
S = "${WORKDIR}/git"

# Specify the installation directory
bindir = "/usr/bin"
sysconfdir = "/etc/ssl/private"

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${B}/acfshell ${D}${bindir}/acfshell
    install -d ${D}${sysconfdir}
}

# Specify the package information
FILES_${PN} = "${bindir}/* ${sysconfdir}/*"

# Enable wrap-based subproject downloading
#EXTRA_OEMESON += "-Dwrap_mode=forcefallback"