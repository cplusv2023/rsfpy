import numpy as np
import sys, io
import matplotlib.pyplot as plt
from matplotlib.backends.backend_svg import RendererSVG
from rsfpy import Rsfarray
import time
from lxml import etree
from matplotlib import transforms

class SvgReRender:
    def __init__(self, svg_str):
        parser = etree.XMLParser(remove_blank_text=False)
        self.root = etree.fromstring(svg_str.encode("utf-8"), parser=parser)
        self.nsmap = self.root.nsmap.copy()
        if None in self.nsmap:
            self.nsmap["svg"] = self.nsmap.pop(None)
        self.cache = {}  # 缓存 {id: element}

    def _get_elem(self, elem_id, tag="g"):
        if elem_id in self.cache:
            return self.cache[elem_id]
        elems = self.root.xpath(f'.//svg:{tag}[@id="{elem_id}"]', namespaces=self.nsmap)
        if not elems:
            raise ValueError(f"未找到 id={elem_id} 的 <{tag}> 元素")
        self.cache[elem_id] = elems[0]
        return elems[0]

    def set_text(self, elem_id, **kargs):
        g_elem = self._get_elem(elem_id, "g")
        text_elem = g_elem.find(".//{*}text")
        if text_elem is None:
            raise ValueError(f"<g id={elem_id}> 内未找到 <text>")
        self._update_elem(text_elem, **kargs)

    def set_path(self, elem_id, **kargs):
        path_elem = self._get_elem(elem_id, "path")
        self._update_elem(path_elem, **kargs)

    def set_img(self, elem_id, **kargs):
        img_elem = self._get_elem(elem_id, "image")
        self._update_elem(img_elem, **kargs)

    def _update_elem(self, elem, **kargs):
        for key, value in kargs.items():
            if value is None:
                continue
            val_str = str(value)
            append_mode = False
            if val_str.startswith("+"):
                append_mode = True
                val_str = val_str[1:]
            if key == "text":
                if append_mode:
                    elem.text = (elem.text or "") + val_str
                else:
                    elem.text = val_str
            else:
                if append_mode:
                    old_val = elem.get(key)
                    if old_val is None:
                        elem.set(key, val_str)
                    else:
                        elem.set(key, old_val + val_str)
                else:
                    elem.set(key, val_str)

    def tostring(self):
        return etree.tostring(self.root, encoding="unicode")


def sf_warning(*msg):
    print("Warning: ", *msg, file=sys.stderr)

def affine_from_parallelograms(src_pts, dst_pts):
    """
    计算从 src_pts 到 dst_pts 的仿射变换矩阵 (3x3)
    
    参数:
        src_pts: [(x0,y0), (x1,y1), (x2,y2), (x3,y3)] 源平行四边形顶点
        dst_pts: [(x0',y0'), (x1',y1'), (x2',y2'), (x3',y3')] 目标平行四边形顶点
    
    返回:
        M: 3x3 仿射矩阵 (numpy.ndarray)
    """
    src = np.array(src_pts, dtype=float)
    dst = np.array(dst_pts, dtype=float)

    # 构造方程 A * params = B
    A = []
    B = []
    for (x, y), (xp, yp) in zip(src, dst):
        A.append([x, y, 1, 0, 0, 0])
        A.append([0, 0, 0, x, y, 1])
        B.append(xp)
        B.append(yp)

    A = np.array(A)
    B = np.array(B)

    # 最小二乘解
    params, _, _, _ = np.linalg.lstsq(A, B, rcond=None)
    a, b, c, d, e, f = params

    M = np.array([[a, b, c],
                  [d, e, f],
                  [0, 0, 1]])
    return transforms.Affine2D(M)
import mpl_toolkits.axisartist as artist

# class DynamicFloatingAxes(artist.Axes):
#     def __init__(self, *args, mode="Y", point1=0.5, point2=0.5, extremes=[0,1,0,1],**kwargs):
#         self.mode = mode
#         self.point1 = point1
#         self.point2 = point2
#         super().__init__(*args, **kwargs)
#         self.extremes = extremes
#         self._xlim = extremes[0:2]
#         self._ylim = extremes[2:4]
#         self._rebuild_grid_helper(self._xlim, self._ylim)
    
#     def get_transform(self):
#         self._rebuild_grid_helper(self._xlim, self._ylim)
#         return self.dtrans + self.transAxes
    
#     @property
#     def aux_trans(self):
#         return self.get_transform()

#     def _rebuild_grid_helper(self, xlim, ylim):
#         minx, maxx = xlim
#         miny, maxy = ylim

#         # 四个角点
#         self.rect_axis = [(minx, miny), (maxx, miny),
#                      (maxx, maxy), (minx, maxy)]
#         if (self.mode == "X"):
#             self.rect_base  = [(0, 0), 
#                         (1, (1 - self.point1)/(1-self.point2) * (1-self.point1)),
#                         (1, self.point1 + (1 - self.point1)/(1-self.point2) * (1-self.point1)), 
#                         (0, self.point1)]
#             self.rect_data = [(0, 0), (1, (1-self.point1)), 
#                          (1, 1), (0, (self.point1))]
#         else:
#             self.rect_base  = [((1 - self.point2)/(1-self.point1) * (1-self.point2), 1),
#                       (self.point2 + (1 - self.point2)/(1-self.point1) * (1-self.point2), 1),
#                       (self.point2, 0),
#                       (0, 0)]
#             self.rect_data = [((1-self.point2), 1), (1, 1), (self.point2, 0), (0, 0)]

#         self.atrans = affine_from_parallelograms(self.rect_axis, self.rect_base)
#         self.dtrans = affine_from_parallelograms(self.rect_axis, self.rect_data)

#         self._grid_helper = artist.floating_axes.GridHelperCurveLinear(
#             self.atrans, extremes=(minx, maxx, miny, maxy)
#         )
#         self.gridlines.set_grid_helper(self._grid_helper)
#         self.clear()

#     # def set_xlim(self, *args, **kwargs):
#     #     ret = super().set_xlim(*args, **kwargs)
#     #     self._xlim = ret
#     #     self._rebuild_grid_helper(self._xlim, self._ylim)
#     #     for artist in self.artists:
#     #         if hasattr(artist, 'set_transform'):
#     #             artist.set_transform(self.aux_trans)
#     #     return ret

#     # def set_ylim(self, *args, **kwargs):
#     #     ret = super().set_ylim(*args, **kwargs)
#     #     self._ylim = ret
#     #     self._rebuild_grid_helper(self._xlim, self._ylim)
#     #     for artist in self.artists:
#     #         if hasattr(artist, 'set_transform'):
#     #             artist.set_transform(self.aux_trans)
#     #     return ret
    
#     def plot(self,*args, **kwargs):
#         transform = kwargs.pop('transform', None)
#         if transform is not None: kwargs.update({'transform': transform})
#         else: kwargs.update({'transform': self.aux_trans})
#         ret = super().plot(*args, **kwargs)
#         return ret

#     def scatter(self,*args, **kwargs):
#         transform = kwargs.pop('transform', None)
#         if transform is not None: kwargs.update({'transform': transform})
#         else: kwargs.update({'transform': self.aux_trans})
#         ret = super().scatter(*args, **kwargs)
#         return ret
    
#     def imshow(self,*args, **kwargs):
#         transform = kwargs.pop('transform', None)
#         if transform is not None: kwargs.update({'transform': transform})
#         else: kwargs.update({'transform': self.get_transform()})
#         ret = super().imshow(*args, **kwargs)
#         return ret
    
#     def loglog(self, *args, **kwargs):
#         transform = kwargs.pop('transform', None)
#         if transform is not None: kwargs.update({'transform': transform})
#         else: kwargs.update({'transform': self.aux_trans})
#         ret = super().loglog(*args, **kwargs)
#         return ret

if __name__ == '__main__' :
    
    plt.rcParams['svg.fonttype'] = 'none'  # 保持文本为 <text> 元素，而非路径

    arr = Rsfarray('cmp1.rsf')

    slice = arr.window(f3=0, n3=1)

    # print(svg_out.getvalue())
    slice1 = arr.window(f3=0, n3=1)
    slice2 = arr.window(n2=1, f2=100)
    slice3 = arr.window(n1=1, f1=100)

    point1, point2 = 0.7, 0.7 # AX1 height and width ratio
    axis1, axis2, axis3 = arr.axis([0,1,2])
    xreverse, yreverse, zreverse = False, True, False
    min1, max1 = axis1.min(), axis1.max()
    min2, max2 = axis2.min(), axis2.max()
    min3, max3 = axis3.min(), axis3.max()


    if xreverse:
        min2, max2 = max2, min2
    if yreverse:
        min1, max1 = max1, min1
    if zreverse:
        min3, max3 = max3, min3

        # for (xdata, ydata) in self.rect_axis:
        #     if self.mode == "Y":
        #         xpt = (xdata - minx) / (maxx - minx)
        #         ypt = (ydata - miny) / (maxy - miny)
        #         ypt = ypt * self.point1 + xpt * (1 - self.point1)/(1 - self.point2) * (1 - self.point1)
        #         self.rect_base.append((xpt, ypt))
        #     else:
        #         xpt = (xdata - minx) / (maxx - minx)
        #         ypt = (ydata - miny) / (maxy - miny)
        #         xpt = xpt * self.point2 + ypt * (1 - self.point2)/(1 - self.point1) * (1 - self.point2)
        #         self.rect_base.append((xpt, ypt))

    # Decide origin of ax2
    # Data rectangle: Upper-left, Upper-right, Bottom-right, Bottom-left
    ax2_rect_axis = [(min3, min1), (max3, min1), (max3, max1), (min3, max1)]
    ax3_rect_axis = [(min2, max3), (max2, max3), (max2, min3), (min2, min3)]
    ax2_rect_base  = [(0, 0), (1, (1 - point1)/(1-point2) * (1-point1)),
                    (1, point1 + (1 - point1)/(1-point2) * (1-point1)), (0, point1)]
    ax3_rect_base  = [((1 - point2)/(1-point1) * (1-point2), 1),
                      (point2 + (1 - point2)/(1-point1) * (1-point2), 1),
                      (point2, 0),
                      (0, 0)]
    ax2_rect_data = [(0, 0), (1, (1-point1)), (1, 1), (0, (point1))]
    ax3_rect_data = [((1-point2), 1), (1, 1), (point2, 0), (0, 0)]


    atrans1 = affine_from_parallelograms(ax2_rect_axis, ax2_rect_base)
    atrans2 = affine_from_parallelograms(ax3_rect_axis, ax3_rect_base)
    dtrans1 = affine_from_parallelograms(ax2_rect_axis, ax2_rect_data)
    dtrans2 = affine_from_parallelograms(ax3_rect_axis, ax3_rect_data)
    
    figure, axbase = plt.subplots(figsize=(6,6), facecolor='none')


    # ax2_grid_helper = artist.floating_axes.GridHelperCurveLinear(atrans1, [min3, max3, min1, max1])
    # ax3_grid_helper = artist.floating_axes.GridHelperCurveLinear(dtrans2, [min2, max2, min3, max3])

    # ax1 = axbase.inset_axes([0, 0, point2, point1], facecolor='none')
    # ax2 = axbase.inset_axes([point2, 0, 1 - point2, 1], facecolor='none',
    #                         # axes_class=artist.Axes, grid_helper = ax2_grid_helper,)
    #                         axes_class=artist.Axes, grid_helper=ax2_grid_helper,)
    # ax3 = axbase.inset_axes([0, point1, 1, 1 - point1], facecolor='none',
    #                         axes_class=artist.Axes, grid_helper=ax3_grid_helper,)

    # # ax2 = axbase.inset_axes([point2, 0, 1-point2, point1], facecolor='none')
    # # ax3 = axbase.inset_axes([0, point1, point2, 1-point1], facecolor='none')
    # minx, maxx, miny, maxy = ax2.get_position().bounds

    # axbase.axis('off')

    # ax2.grid(True, which='both', color='lightgray', linestyle='--', linewidth=0.5)
    # ax3.grid(True, which='both', color='lightgray', linestyle='--', linewidth=0.5)

    # ax2.transData1 = dtrans1 + ax2.transAxes
    # ax2._imshow_orig = ax2.imshow
    # ax3.transData1 = dtrans2 + ax3.transAxes
    # ax3._imshow_orig = ax3.imshow

    # def imshow2(*args, **kwargs):
    #     transform = kwargs.pop('transform', None)
    #     if transform is not None:
    #         kwargs['transform'] = transform
    #     else:
    #         kwargs['transform'] = ax2.transData1
    #     # 调用原始方法，而不是 ax2.imshow（避免递归）
    #     return ax2._imshow_orig(*args, **kwargs)
    # def imshow3(*args, **kwargs):
    #     transform = kwargs.pop('transform', None)
    #     if transform is not None:
    #         kwargs['transform'] = transform
    #     else:
    #         kwargs['transform'] = ax3.transData1
    #     # 调用原始方法，而不是 ax2.imshow（避免递归）
    #     return ax3._imshow_orig(*args, **kwargs)

    # def null(*args, **kwargs):
    #     # Do nothing
    #     return None
    # ax2.imshow = imshow2
    # ax3.imshow = imshow3
    # ax2.set_xlim = null
    # ax2.set_ylim = null

      
    # ax2.scatter([0.055], [.1], color='red')
    # ax2.imshow(slice1, aspect='auto',
    #                extent=[min3, max3, min1, max1],)
    # ax3.imshow(slice2, aspect='auto',
    #                extent=[min2, max2, min3, max3],)

    # # slice3.grey(ax=ax2, show=False, min1=min1, max1=max1, min2=min3, max2=max3)

    # # slice3.grey(ax=ax2, min1=min1, max1=max1, min2=min3, max2=max3)
    # # slice2.grey(ax=ax3, min1=min2, max1=max2, min2=min3, max2=max3)
    # ax2.set_ylim(0, 1)
    # ax2.set_xlim(0, 1)
    # ax3.set_ylim(0, 1)
    # ax3.set_xlim(0, 1)

    gattr = arr.grey3(ax=axbase, colorbar=True, point2=0.4, point1=0.7,
                      xreverse=False, yreverse=False, zreverse=True,
                      frame1=150,frame2=100, frame3=5,)
    plt.tight_layout()
    plt.savefig("test_grey3.svg", format="svg", facecolor='none')
    plt.show()
