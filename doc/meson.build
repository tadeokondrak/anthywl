if scdoc.found()
    scdoc_prog = find_program(scdoc.get_variable(pkgconfig: 'scdoc'), native: true)
    mandir = get_option('mandir')
    man_files = [
        'anthywl.1.scd',
        'anthywl.5.scd',
    ]

    foreach filename : man_files
        topic = filename.split('.')[-3].split('/')[-1]
        section = filename.split('.')[-2]
        output = '@0@.@1@'.format(topic, section)

        custom_target(
            output,
            input: filename,
            output: output,
            command: scdoc_prog,
            install: true,
            feed: true,
            capture: true,
            install_dir: '@0@/man@1@'.format(mandir, section)
        )
    endforeach
endif
