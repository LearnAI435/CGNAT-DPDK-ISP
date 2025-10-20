project('dpdk-cgnat', 'c',
    version: '1.0.0',
    license: 'MIT',
    default_options: [
        'buildtype=release',
        'optimization=3',
        'warning_level=3',
        'c_std=c11'
    ]
)

# DPDK dependency
dpdk_dep = dependency('libdpdk', required: true)

# Other dependencies
threads_dep = dependency('threads')
math_dep = meson.get_compiler('c').find_library('m', required: true)
pcap_dep = dependency('pcap', required: false)
yaml_dep = dependency('yaml-0.1', required: true)

# Include directories
inc = include_directories('include')

# Common compile flags
add_project_arguments([
    '-march=native',
    '-DALLOW_EXPERIMENTAL_API',
    '-D_GNU_SOURCE',
], language: 'c')

# Source files
dpdk_sources = files(
    'src/dpdk/runtime.c',
    'src/dpdk/port_config.c',
)

nat_sources = files(
    'src/nat/engine.c',
    'src/nat/hash_table.c',
    'src/nat/port_pool.c',
    'src/nat/timer_wheel.c',
)

control_sources = files(
    'src/control/config.c',
    'src/control/api_server.c',
)

telemetry_sources = files(
    'src/telemetry/metrics.c',
    'src/telemetry/prometheus.c',
    'src/telemetry/stats_collector.c',
)

logging_sources = files(
    'src/logging/logger.c',
    'src/logging/ring_buffer.c',
)

main_sources = files('src/main.c')

# Build main executable
executable('dpdk-cgnat',
    sources: [
        main_sources,
        dpdk_sources,
        nat_sources,
        control_sources,
        telemetry_sources,
        logging_sources,
    ],
    include_directories: inc,
    dependencies: [dpdk_dep, threads_dep, math_dep, yaml_dep],
    install: true,
)

# Configuration files
install_data('config/cgnat.yaml', install_dir: get_option('sysconfdir') / 'dpdk-cgnat')

# Documentation
install_data('README.md', 'docs/DEPLOYMENT.md', install_dir: get_option('datadir') / 'doc' / 'dpdk-cgnat')
