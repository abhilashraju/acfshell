project('acfshell', 'cpp', version: '1.0.0', default_options: ['cpp_std=c++23','cpp_args=-Wno-subobject-linkage'])
boost_dep = dependency('boost', modules: ['coroutine'], required: true)
openssl_dep = dependency('openssl', required: true)
sdbusplus_dep = dependency('sdbusplus', required: false, include_type: 'system')
executable('acfshell', 
            'script_runner.cpp', 
            dependencies: [boost_dep,openssl_dep,sdbusplus_dep],
            install: true,
            install_dir: '/usr/bin')
install_data('service/xyz.openbmc_project.acfshell.service', install_dir: '/etc/systemd/system')
install_data('service/xyz.openbmc_project.acfshell.conf',install_dir:'/etc/dbus-1/system.d/')