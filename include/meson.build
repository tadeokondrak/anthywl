foreach file: ['default_config']
    anthywl_src += custom_target(file,
        input: '..' / file,
        output: file + '.inc',
        command: [file2string, '@INPUT@', '@OUTPUT@'],
    )
endforeach

anthywl_inc += include_directories('.')
