project(
    'mx4hyprland',
    'cpp',
    version: '2.0.0',
    default_options: [
        'cpp_std=c++23',
        'warning_level=3',
        'werror=true',
        'optimization=2',
        'b_lto=true',
        'prefix=' + get_option('prefix'),
    ],
    meson_version: '>=1.1.0',
)

cpp = meson.get_compiler('cpp')

add_project_arguments(
    cpp.get_supported_arguments(
        '-Wno-missing-field-initializers',
        '-Wconversion',
        '-Wshadow',
        '-Wnon-virtual-dtor',
        '-Wold-style-cast',
        '-Wcast-align',
        '-Wunused',
        '-Woverloaded-virtual',
        '-Wpedantic',
        '-Wnull-dereference',
        '-Wdouble-promotion',
        '-Wformat=2',
        '-Wimplicit-fallthrough',
    ),
    language: 'cpp',
)

hidapi_dep = dependency('hidapi-hidraw', required: false)
if not hidapi_dep.found()
    hidapi_dep = dependency('hidapi-libusb')
endif

libudev_dep = dependency('libudev')

tomlplusplus_dep = dependency('tomlplusplus', required: false)
if not tomlplusplus_dep.found()
    tomlplusplus_dep = dependency('toml++', required: false)
endif
if not tomlplusplus_dep.found()
    tomlplusplus_proj = subproject('tomlplusplus', default_options: ['default_library=static'])
    tomlplusplus_dep = tomlplusplus_proj.get_variable('tomlplusplus_dep')
endif

sources = files(
    'src/main.cpp',
    'src/config.cpp',
    'src/mx_master_4.cpp',
    'src/haptic_manager.cpp',
    'src/hyprland_listener.cpp',
    'src/ipc_server.cpp',
)

include_dirs = include_directories('include')

executable(
    'mx4hyprland',
    sources,
    include_directories: include_dirs,
    dependencies: [
        hidapi_dep,
        tomlplusplus_dep,
        libudev_dep,
    ],
    install: true,
)

install_data(
    'config.toml',
    install_dir: get_option('sysconfdir') / 'mx4hyprland',
    install_mode: 'rw-r--r--',
)

systemd_user_dir = get_option('prefix') / 'lib' / 'systemd' / 'user'
if get_option('user-install')
    systemd_user_dir = get_option('datadir') / 'systemd' / 'user'
endif

configure_file(
    input: 'mx-haptics.service.in',
    output: 'mx-haptics.service',
    configuration: {
        'bindir': get_option('prefix') / get_option('bindir'),
    },
    install: true,
    install_dir: systemd_user_dir,
)

install_data(
    '99-logitech-mx_master_4.rules',
    install_dir: '/etc/udev/rules.d',
    install_mode: 'rw-r--r--',
)

meson.add_install_script('scripts/post-install.sh')
