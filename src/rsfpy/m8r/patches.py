# Rsfpy: pacth for Madagascar SConstruct flow
import sys

try:
    from rsf.proj import project
    import os
    WhereIs = project.WhereIs

    def get_bin_from_package():
        bindir = WhereIs("rsfgrey")
        if bindir is not None:
            return os.path.dirname(bindir)
        pkg_dir = os.path.dirname(os.path.dirname(__file__))
        prefix = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(pkg_dir))))  # <prefix>
        return os.path.join(prefix, "bin")

    project.Append(ENV= {'PATH':project['ENV']['PATH'] + os.pathsep + get_bin_from_package()})

    def svgPlot(*args, **kargs):
        suffix = '.svg'
        kargs.update({'suffix': suffix})
        target = args[0]
        if target.endswith('.svg'):
            target2 = target
        else:
            target2 = target + suffix
        if len(args) > 2:
            if 'rsfsvgpen' in args[2]:
                kargs.update({'src_suffix': '.svg'})
        return project.Plot(*args, **kargs)

    def svgResult(*args, **kargs):
        suffix = '.svg'
        kargs.update({'suffix': suffix})
        target = args[0]
        target2 = os.path.join(project.resdir, target) + suffix
        if len(args) > 2:
            if 'rsfsvgpen' in args[2]:
                kargs.update({'src_suffix': '.svg'})
        viewer = WhereIs('svgviewer') or WhereIs('eog') or WhereIs('inkscape') \
                 or WhereIs('xdg-open') or WhereIs('open')
        if viewer:
            cmd = project.Command(f'{target}.view', target2, f'{viewer} $SOURCES')
        else:
            cmd = project.Command(f'{target}.view', target2,
                                  f'echo "No SVG viewer found to open $SOURCES". Try install eog or use web browser.')
        project.view.append(cmd)

        # flip
        locked = os.path.join(project.figdir, target + '.svg')
        if viewer:
            flip_cmd = project.Command(f'{target}.flip', [target2], f'{viewer} $SOURCES {locked}')
        else:
            flip_cmd = project.Command(f'{target}.flip', [target2],
                                       f'echo "No SVG viewer found to open ${{SOURCES[0]}}". Try install eog or use web browser.')
        return project.Result(*args, **kargs)
except ImportError:
    print("Failed to import rsf.proj. Check if madagascar is correctly installed!", file=sys.stderr)
    sys.exit(1)
except Exception as e:
    print("Failed to load rsfpy:", e, file=sys.stderr)