wl_protocol_dir = wayland_protos.get_pkgconfig_variable('pkgdatadir')

wayland_scanner = find_program('wayland-scanner')

# should check wayland_scanner's version, but it is hard to get
if wayland_server.version().version_compare('>=1.14.91')
	code_type = 'private-code'
else
	code_type = 'code'
endif

protocols = [
	['both', [wl_protocol_dir, 'stable/xdg-shell', 'xdg-shell']],
	['server', [wl_protocol_dir, 'unstable/xdg-shell', 'xdg-shell-unstable-v6']],
	['both', [wl_protocol_dir, 'unstable/xdg-output', 'xdg-output-unstable-v1']],
	['server', [wl_protocol_dir, 'unstable/pointer-constraints', 'pointer-constraints-unstable-v1']],
	['client', ['idle']],
	['both', ['wlr-layer-shell-unstable-v1']],
	['both', ['wlr-input-inhibitor-unstable-v1']],
]

client_protos_src = []
client_protos_headers = []

server_protos_src = []
server_protos_headers = []

foreach p : protocols
	path_parts = p[1]
	xml = join_paths(path_parts) + '.xml'
	base_name = path_parts[-1]
	users = p[0]

	src = custom_target(
		xml.underscorify() + '_protocol_c',
		output : '@0@-protocol.c'.format(base_name),
		input : xml,
		command : [wayland_scanner, code_type, '@INPUT@', '@OUTPUT@'],
	)

	if users == 'client' or users == 'both'
		client_protos_src += src
		client_protos_headers += custom_target(
			xml.underscorify() + '_client_h',
			output : '@0@-client-protocol.h'.format(base_name),
			input : xml,
			command : [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
		)
	endif

	if users == 'server' or users == 'both'
		server_protos_src += src
		server_protos_headers += custom_target(
			xml.underscorify() + '_server_h',
			output : '@0@-protocol.h'.format(base_name),
			input : xml,
			command : [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
		)
	endif
endforeach

lib_client_protos = static_library(
	'client_protos',
	client_protos_src + client_protos_headers,
	dependencies: [wayland_client]
) # for the include directory

client_protos = declare_dependency(
	link_with: lib_client_protos,
	sources: client_protos_headers,
)

lib_server_protos = static_library(
	'server_protos',
	server_protos_src + server_protos_headers,
	dependencies: [wayland_server]
) # for the include directory

server_protos = declare_dependency(
	link_with: lib_server_protos,
	sources: server_protos_headers,
)
