# Rsfpy: pacth for Madagascar SConstruct flow

try:
    from rsf.proj import project, WhereIs
    import os


    def svgResult(*args, **kargs):
        suffix = '.svg'
        kargs.update({'suffix': suffix})
        target = args[0]
        target2 = os.path.join(project.resdir, target) + suffix
        if len(args) > 2:
            if 'rsfsvgpen' in args[2]:
                kargs.update({'src_suffix': '.svg'})
        viewer = WhereIs('eog') or WhereIs('inkscape') \
                 or WhereIs('xdg-open') or WhereIs('open')
        if viewer:
            cmd = project.Command(f'{target}.view', target2, f'{viewer} $SOURCES')
        else:
            cmd = project.Command(f'{target}.view', target2,
                                  f'echo "No SVG viewer found to open $SOURCES". Try install eog or use web browser.')
        project.view.append(cmd)

        # flip (test using eog)
        locked = os.path.join(project.figdir, target + '.svg')
        if viewer:
            flip_cmd = project.Command(f'{target}.flip', [target2], f'{viewer} $SOURCES {locked}')
        else:
            flip_cmd = project.Command(f'{target}.flip', [target2],
                                       f'echo "No SVG viewer found to open ${{SOURCES[0]}}". Try install eog or use web browser.')
        return project.Result(*args, **kargs)
except:
    exit(1)