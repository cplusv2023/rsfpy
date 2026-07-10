# Rsfpy: patch for Madagascar SConstruct flow
import os
import sys
import types

try:
    import rsf.proj as rsf_proj
    from rsf.proj import project

    WhereIs = project.WhereIs
    vpsuffix = rsf_proj.vpsuffix
    printer = rsf_proj.printer
    combine = rsf_proj.combine

    def get_bin_from_package():
        bindir = WhereIs("rsfgrey")
        if bindir is not None:
            return os.path.dirname(bindir)
        pkg_dir = os.path.dirname(os.path.dirname(__file__))
        prefix = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(pkg_dir))))
        return os.path.join(prefix, "bin")

    project.Append(ENV={'PATH': project['ENV']['PATH'] + os.pathsep + get_bin_from_package()})

    project.Append(ENV={
        'SSH_CLIENT': os.environ.get('SSH_CLIENT', ''),
        'SSH_TTY': os.environ.get('SSH_TTY', ''),
        'SSH_CONNECTION': os.environ.get('SSH_CONNECTION', ''),
    })

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
        'RSFPY_VPLVIEWER_CONVERTER',
        'RSFVPLOPTS',
    ]
    project.Append(ENV={key: os.environ.get(key, '') for key in inherited_env})

    def _vpl_viewer(self):
        return WhereIs('vplviewer') or self.sfpen

    def _svg_viewer():
        return (
            WhereIs('svgviewer')
            or WhereIs('eog')
            or WhereIs('inkscape')
            or WhereIs('xdg-open')
            or WhereIs('open')
        )

    def patchedPlot(self, target, source, flow=None, suffix=vpsuffix, vppen=None,
                    view=None, **kw):
        if not flow:
            flow = source
            source = target
        if 'Annotate' == flow:
            if not type(source) is list:
                source = source.split()
            flow = os.path.join(self.bindir, 'vpannotate') + \
                ' text=${SOURCES[1]} batch=y ${SOURCES[0]} $TARGET'
            kw.update({'src_suffix': vpsuffix, 'stdin': 0, 'stdout': -1})
        elif flow in combine:
            if not type(source) is list:
                source = source.split()
            flow = combine[flow](*[self.vppen, len(source)])
            if vppen:
                flow = flow + ' ' + vppen
            kw.update({'src_suffix': vpsuffix, 'stdin': 0})
        if view:
            if suffix == vpsuffix and 'matplotlib' not in flow:
                viewer = _vpl_viewer(self)
                if viewer == self.sfpen:
                    flow = flow + ' | %s pixmaps=y' % self.sfpen
                else:
                    flow = flow + ' | %s' % viewer
            kw.update({'stdout': -1})
        kw.update({'suffix': suffix})
        return self.Flow(*(target, source, flow), **kw)

    def patchedResult(self, target, source, flow=None, suffix=vpsuffix, **kw):
        if not flow:
            flow = source
            source = target
        target2 = os.path.join(self.resdir, target)
        if 'matplotlib' in flow:
            pngflow = flow + ' format=png'
            pngsuffix = '.png'
            kw.update({'suffix': pngsuffix})
            self.Plot(*(target, source, pngflow), **kw)

            flow += ' format=pdf'
            suffix = '.pdf'

        kw.update({'suffix': suffix})
        plot = self.Plot(*(target2, source, flow), **kw)
        target2 = target2 + suffix

        if suffix == vpsuffix:
            viewer = _vpl_viewer(self)
        elif suffix == '.svg':
            viewer = _svg_viewer()
        elif suffix == '.pdf':
            viewer = WhereIs('acroread') or WhereIs('kpdf') \
                or WhereIs('evince') or WhereIs('xpdf') or WhereIs('gv') \
                or WhereIs('open')
        elif suffix == '.eps':
            viewer = WhereIs('evince') or WhereIs('gv') or WhereIs('open')
        else:
            viewer = None

        if viewer:
            view = self.Command(target + '.view', plot, viewer + " $SOURCES",
                                src_suffix=suffix)
            self.view.append(view)

        prnt = self.Command(target + '.print', plot,
                            self.pspen + " printer=%s $SOURCES" % printer,
                            src_suffix=vpsuffix)
        self.prnt.append(prnt)

        locked = os.path.join(self.figdir, target + suffix)
        self.InstallAs(locked, target2)
        self.Alias(target + '.lock', locked)
        self.lock.append(locked)

        if suffix == vpsuffix:
            viewer = _vpl_viewer(self)
            if viewer == self.sfpen:
                self.Command(
                    target + '.flip',
                    target2,
                    '%s $SOURCE %s' % (self.sfpen, locked),
                )
            else:
                self.Command(
                    target + '.flip',
                    target2,
                    '%s $SOURCE %s' % (viewer, locked),
                )

        elif suffix == '.svg':
            svgviewer = WhereIs('svgviewer')
            if svgviewer:
                self.Command(
                    target + '.flip',
                    target2,
                    '%s $SOURCE %s' % (svgviewer, locked),
                )

        test = self.Test('.test_' + target, target2,
                         figdir=self.figs, bindir=self.bindir)
        self.test.append(test)
        self.Alias(target + '.test', test)
        self.rest.append(target)
        return plot

    project.Plot = types.MethodType(patchedPlot, project)
    project.Result = types.MethodType(patchedResult, project)

    def Plot(target, source, flow=None, **kw):
        return project.Plot(*(target, source, flow), **kw)

    def Result(target, source, flow=None, **kw):
        return project.Result(*(target, source, flow), **kw)

    rsf_proj.Plot = Plot
    rsf_proj.Result = Result

    def svgPlot(*args, **kargs):
        if len(args) < 2 and not kargs.get('flow', None):
            raise ValueError(f"No flow for building {args[0]}?")
        elif len(args) > 2 and kargs.get('flow', None) is not None:
            raise ValueError(f"Too many arguments for building {args[0]}.")
        kargs.update({'suffix': '.svg'})
        target = args[0]
        if type(target) == str:
            target = target.split()
        if type(target) in (list, tuple):
            target = target[0]

        if len(args) > 2:
            if 'rsfsvgpen' in args[2]:
                kargs.update({'src_suffix': '.svg'})
        else:
            args = list(args)
            args.insert(1, target)
        return project.Plot(*args, **kargs)

    def svgResult(*args, **kargs):
        if len(args) < 2 and not kargs.get('flow', None):
            raise ValueError(f"No flow for building {args[0]}?")
        elif len(args) > 2 and kargs.get('flow', None) is not None:
            raise ValueError(f"Too many arguments for building {args[0]}.")
        kargs.update({'suffix': '.svg'})
        target = args[0]
        if type(target) == str:
            target = target.split()
        if type(target) in (list, tuple):
            target = target[0]

        if len(args) > 2:
            if 'rsfsvgpen' in args[2]:
                kargs.update({'src_suffix': '.svg'})
        else:
            args = list(args)
            args.insert(1, target)
        return project.Result(*args, **kargs)

except ImportError:
    print("Failed to import rsf.proj. Check if madagascar is correctly installed!", file=sys.stderr)
    sys.exit(1)
except Exception as e:
    print("Failed to load rsfpy:", e, file=sys.stderr)
