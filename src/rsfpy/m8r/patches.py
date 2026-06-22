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

    # SSH status env
    project.Append(ENV={'SSH_CLIENT': os.environ.get('SSH_CLIENT', ''),
                        'SSH_TTY': os.environ.get('SSH_TTY', ''),
                        'SSH_CONNECTION': os.environ.get('SSH_CONNECTION', '')})

    # Viewer/client environment used by rsfpy GUI display and SSH-agent flows.
    # Do not propagate internal secret-bearing askpass variables here.
    inherited_env = [
        'DISPLAY',
        'WAYLAND_DISPLAY',
        'SSH_AUTH_SOCK',
        'SSH_ASKPASS',
        'SSH_ASKPASS_REQUIRE',
        'RSFVIEW_PORT',
        'RSFVIEW_TOKEN',
        'RSFPY_SVGVIEWER_REMOTE',
        'RSFPY_SVGVIEWER_BACKEND',
        'RSFPY_SVGVIEWER_CLIENT_CMD',
    ]
    project.Append(ENV={key: os.environ.get(key, '') for key in inherited_env})

    def svgPlot(*args, **kargs):
        if len(args) < 2 and not kargs.get('flow', None):
            raise ValueError(f"No flow for building {args[0]}?")
        elif len(args) > 2 and kargs.get('flow', None) is not None:
            raise ValueError(f"Too many arguments for building {args[0]}.")
        suffix = '.svg'
        kargs.update({'suffix': suffix})
        target = args[0]
        if type(target) == str:
            target = target.split()
        if type(target) in (list, tuple):
            if len(target) > 1:
                side_target = target[1:]
            else:
                side_target = None
            target = target[0]
        else:
            side_target = None

        if len(args) > 2: # Got target, source and flow
            if 'rsfsvgpen' in args[2]:
                kargs.update({'src_suffix': '.svg'})
        else: # Got target and flow, expect source = target (basename)
            args = list(args)
            args.insert(1, target)
        return project.Plot(*args, **kargs)

    def svgResult(*args, **kargs):
        if len(args) < 2 and not kargs.get('flow', None):
            raise ValueError(f"No flow for building {args[0]}?")
        elif len(args) > 2 and kargs.get('flow', None) is not None:
            raise ValueError(f"Too many arguments for building {args[0]}.")
        suffix = '.svg'
        kargs.update({'suffix': suffix})
        target = args[0]
        if type(target) == str:
            target = target.split()
        if type(target) in (list, tuple):
            if len(target) > 1:
                side_target = target[1:]
            else:
                side_target = None
            target = target[0]
        else:
            side_target = None
        target2 = os.path.join(project.resdir, target) + suffix

        if len(args) > 2: # Got target, source and flow
            if 'rsfsvgpen' in args[2]:
                kargs.update({'src_suffix': '.svg'})
        else: # Got target and flow, expect source = target (basename)
            args = list(args)
            args.insert(1, target)
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
