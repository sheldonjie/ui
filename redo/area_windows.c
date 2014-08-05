/* 24 march 2014 */

/* TODO either strip the // comments or find out if --std=c99 is safe for cgo */

#include "winapi_windows.h"
#include "_cgo_export.h"

#define areaWindowClass L"gouiarea"

static void getScrollPos(HWND hwnd, int *xpos, int *ypos)
{
	SCROLLINFO si;

	ZeroMemory(&si, sizeof (SCROLLINFO));
	si.cbSize = sizeof (SCROLLINFO);
	si.fMask = SIF_POS | SIF_TRACKPOS;
	if (GetScrollInfo(hwnd, SB_HORZ, &si) == 0)
		xpanic("error getting horizontal scroll position for Area", GetLastError());
	*xpos = si.nPos;
	// MSDN example code reinitializes this each time, so we'll do it too just to be safe
	ZeroMemory(&si, sizeof (SCROLLINFO));
	si.cbSize = sizeof (SCROLLINFO);
	si.fMask = SIF_POS | SIF_TRACKPOS;
	if (GetScrollInfo(hwnd, SB_VERT, &si) == 0)
		xpanic("error getting vertical scroll position for Area", GetLastError());
	*ypos = si.nPos;
}

#define areaBackgroundBrush ((HBRUSH) (COLOR_BTNFACE + 1))

static void paintArea(HWND hwnd, void *data)
{
	RECT xrect;
	PAINTSTRUCT ps;
	HDC hdc;
	HDC rdc;
	HBITMAP rbitmap, prevrbitmap;
	RECT rrect;
	BITMAPINFO bi;
	VOID *ppvBits;
	HBITMAP ibitmap;
	HDC idc;
	HBITMAP previbitmap;
	BLENDFUNCTION blendfunc;
	void *i;
	intptr_t dx, dy;
	int hscroll, vscroll;

	// TRUE here indicates a
	if (GetUpdateRect(hwnd, &xrect, TRUE) == 0)
		return;		// no update rect; do nothing

	getScrollPos(hwnd, &hscroll, &vscroll);

	i = doPaint(&xrect, hscroll, vscroll, data, &dx, &dy);
	if (i == NULL)		// cliprect empty
		return;
	// don't convert to BRGA just yet; see below

	// TODO don't do the above, but always draw the background color?

	hdc = BeginPaint(hwnd, &ps);
	if (hdc == NULL)
		xpanic("error beginning Area repaint", GetLastError());

	// very big thanks to Ninjifox for suggesting this technique and helping me go through it

	// first let's create the destination image, which we fill with the windows background color
	// this is how we fake drawing the background; see also http://msdn.microsoft.com/en-us/library/ms969905.aspx
	rdc = CreateCompatibleDC(hdc);
	if (rdc == NULL)
		xpanic("error creating off-screen rendering DC", GetLastError());
	// the bitmap has to be compatible with the window
	// if we create a bitmap compatible with the DC we just created, it'll be monochrome
	// thanks to David Heffernan in http://stackoverflow.com/questions/23033636/winapi-gdi-fillrectcolor-btnface-fills-with-strange-grid-like-brush-on-window
	rbitmap = CreateCompatibleBitmap(hdc, xrect.right - xrect.left, xrect.bottom - xrect.top);
	if (rbitmap == NULL)
		xpanic("error creating off-screen rendering bitmap", GetLastError());
	prevrbitmap = (HBITMAP) SelectObject(rdc, rbitmap);
	if (prevrbitmap == NULL)
		xpanic("error connecting off-screen rendering bitmap to off-screen rendering DC", GetLastError());
	rrect.left = 0;
	rrect.right = xrect.right - xrect.left;
	rrect.top = 0;
	rrect.bottom = xrect.bottom - xrect.top;
	if (FillRect(rdc, &rrect, areaBackgroundBrush) == 0)
		xpanic("error filling off-screen rendering bitmap with the system background color", GetLastError());

	// now we need to shove realbits into a bitmap
	// technically bitmaps don't know about alpha; they just ignore the alpha byte
	// AlphaBlend(), however, sees it - see http://msdn.microsoft.com/en-us/library/windows/desktop/dd183352%28v=vs.85%29.aspx
	ZeroMemory(&bi, sizeof (BITMAPINFO));
	bi.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
	// TODO check types
	bi.bmiHeader.biWidth = dx;
	bi.bmiHeader.biHeight = -dy;			// negative height to force top-down drawing;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	bi.bmiHeader.biSizeImage = (DWORD) (dx * dy * 4);
	// this is all we need, but because this confused me at first, I will say the two pixels-per-meter fields are unused (see http://blogs.msdn.com/b/oldnewthing/archive/2013/05/15/10418646.aspx and page 581 of Charles Petzold's Programming Windows, Fifth Edition)
	// now for the trouble: CreateDIBSection() allocates the memory for us...
	ibitmap = CreateDIBSection(NULL,		// Ninjifox does this, so do some wine tests (http://source.winehq.org/source/dlls/gdi32/tests/bitmap.c#L725, thanks vpovirk in irc.freenode.net/#winehackers) and even Raymond Chen (http://blogs.msdn.com/b/oldnewthing/archive/2006/11/16/1086835.aspx), so.
		&bi, DIB_RGB_COLORS, &ppvBits, 0, 0);
	if (ibitmap == NULL)
		xpanic("error creating HBITMAP for image returned by AreaHandler.Paint()", GetLastError());

	// now we have to do TWO MORE things before we can finally do alpha blending
	// first, we need to load the bitmap memory, because Windows makes it for us
	// the pixels are arranged in RGBA order, but GDI requires BGRA
	// this turns out to be just ARGB in little endian; let's convert into this memory
	dotoARGB(i, (void *) ppvBits);

	// the second thing is... make a device context for the bitmap :|
	// Ninjifox just makes another compatible DC; we'll do the same
	idc = CreateCompatibleDC(hdc);
	if (idc == NULL)
		xpanic("error creating HDC for image returned by AreaHandler.Paint()", GetLastError());
	previbitmap = (HBITMAP) SelectObject(idc, ibitmap);
	if (previbitmap == NULL)
		xpanic("error connecting HBITMAP for image returned by AreaHandler.Paint() to its HDC", GetLastError());

	// AND FINALLY WE CAN DO THE ALPHA BLENDING!!!!!!111
	blendfunc.BlendOp = AC_SRC_OVER;
	blendfunc.BlendFlags = 0;
	blendfunc.SourceConstantAlpha = 255;		// only use per-pixel alphas
	blendfunc.AlphaFormat = AC_SRC_ALPHA;	// premultiplied
	if (AlphaBlend(rdc, 0, 0, (int) dx, (int) dy,		// destination
		idc, 0, 0, (int) dx, (int)dy,				// source
		blendfunc) == FALSE)
		xpanic("error alpha-blending image returned by AreaHandler.Paint() onto background", GetLastError());

	// and finally we can just blit that into the window
	if (BitBlt(hdc, xrect.left, xrect.top, xrect.right - xrect.left, xrect.bottom - xrect.top,
		rdc, 0, 0,			// from the rdc's origin
		SRCCOPY) == 0)
		xpanic("error blitting Area image to Area", GetLastError());

	// now to clean up
	if (SelectObject(idc, previbitmap) != ibitmap)
		xpanic("error reverting HDC for image returned by AreaHandler.Paint() to original HBITMAP", GetLastError());
	if (SelectObject(rdc, prevrbitmap) != rbitmap)
		xpanic("error reverting HDC for off-screen rendering to original HBITMAP", GetLastError());
	if (DeleteObject(ibitmap) == 0)
		xpanic("error deleting HBITMAP for image returned by AreaHandler.Paint()", GetLastError());
	if (DeleteObject(rbitmap) == 0)
		xpanic("error deleting HBITMAP for off-screen rendering", GetLastError());
	if (DeleteDC(idc) == 0)
		xpanic("error deleting HDC for image returned by AreaHandler.Paint()", GetLastError());
	if (DeleteDC(rdc) == 0)
		xpanic("error deleting HDC for off-screen rendering", GetLastError());

	EndPaint(hwnd, &ps);
}

static SIZE getAreaControlSize(HWND hwnd)
{
	RECT rect;
	SIZE size;

	if (GetClientRect(hwnd, &rect) == 0)
		xpanic("error getting size of actual Area control", GetLastError());
	size.cx = (LONG) (rect.right - rect.left);
	size.cy = (LONG) (rect.bottom - rect.top);
	return size;
}

static void scrollArea(HWND hwnd, void *data, WPARAM wParam, int which)
{
	SCROLLINFO si;
	SIZE size;
	LONG cwid, cht;
	LONG pagesize, maxsize;
	LONG newpos;
	LONG delta;
	LONG dx, dy;

	size = getAreaControlSize(hwnd);
	cwid = size.cx;
	cht = size.cy;
	if (which == SB_HORZ) {
		pagesize = cwid;
		maxsize = areaWidthLONG(data);
	} else if (which == SB_VERT) {
		pagesize = cht;
		maxsize = areaHeightLONG(data);
	} else
		xpanic("invalid which sent to scrollArea()", 0);

	ZeroMemory(&si, sizeof (SCROLLINFO));
	si.cbSize = sizeof (SCROLLINFO);
	si.fMask = SIF_POS | SIF_TRACKPOS;
	if (GetScrollInfo(hwnd, which, &si) == 0)
		xpanic("error getting current scroll position for scrolling", GetLastError());

	newpos = (LONG) si.nPos;
	switch (LOWORD(wParam)) {
	case SB_LEFT:			// also SB_TOP (TODO will C let me?)
		newpos = 0;
		break;
	case SB_RIGHT:		// also SB_BOTTOM
		// see comment in adjustAreaScrollbars() below
		newpos = maxsize - pagesize;
		break;
	case SB_LINELEFT:		// also SB_LINEUP
		newpos--;
		break;
	case SB_LINERIGHT:		// also SB_LINEDOWN
		newpos++;
		break;
	case SB_PAGELEFT:		// also SB_PAGEUP
		newpos -= pagesize;
		break;
	case SB_PAGERIGHT:	// also SB_PAGEDOWN
		newpos += pagesize;
		break;
	case SB_THUMBPOSITION:
		// raymond chen says to just set the newpos to the SCROLLINFO nPos for this message; see http://blogs.msdn.com/b/oldnewthing/archive/2003/07/31/54601.aspx and http://blogs.msdn.com/b/oldnewthing/archive/2003/08/05/54602.aspx
		// do nothing here; newpos already has nPos
		break;
	case SB_THUMBTRACK:
		newpos = (LONG) si.nTrackPos;
	}
	// otherwise just keep the current position (that's what MSDN example code says, anyway)

	// make sure we're not out of range
	if (newpos < 0)
		newpos = 0;
	if (newpos > (maxsize - pagesize))
		newpos = maxsize - pagesize;

	// this would be where we would put a check to not scroll if the scroll position changed, but see the note about SB_THUMBPOSITION above: Raymond Chen's code always does the scrolling anyway in this case

	delta = -(newpos - si.nPos);		// negative because ScrollWindowEx() scrolls in the opposite direction
	dx = delta;
	dy = 0;
	if (which == SB_VERT) {
		dx = 0;
		dy = delta;
	}
	if (ScrollWindowEx(hwnd,
		dx, dy,		// TODO correct types
		// these four change what is scrolled and record info about the scroll; we're scrolling the whole client area and don't care about the returned information here
		// TODO pointers?
		0, 0, 0, 0,
		// mark the remaining rect as needing redraw and erase...
		SW_INVALIDATE | SW_ERASE) == ERROR)
			xpanic("error scrolling Area", GetLastError());
	// ...but don't redraw the window yet; we need to apply our scroll changes

	// we actually have to commit the change back to the scrollbar; otherwise the scroll position will merely reset itself
	ZeroMemory(&si, sizeof (SCROLLINFO));
	si.cbSize = sizeof (SCROLLINFO);
	si.fMask = SIF_POS;
	si.nPos = (int) newpos;
	// TODO double-check that this doesn't return an error
	SetScrollInfo(hwnd, which, &si, TRUE);		// redraw scrollbar

	// NOW redraw it
	if (UpdateWindow(hwnd) == 0)
		panic("error updating Area after scrolling", GetLastError());
}

static void adjustAreaScrollbars(HWND hwnd, void *data)
{
	SCROLLINFO si;
	SIZE size;
	LONG cwid, cht;

	size = getAreaControlSize(hwnd);
	cwid = size.cx;
	cht = size.cy;

	// the trick is we want a page to be the width/height of the visible area
	// so the scroll range would go from [0..image_dimension - control_dimension]
	// but judging from the sample code on MSDN, we don't need to do this; the scrollbar will do it for us
	// we DO need to handle it when scrolling, though, since the thumb can only go up to this upper limit

	// have to do horizontal and vertical separately
	ZeroMemory(&si, sizeof (SCROLLINFO));
	si.cbSize = sizeof (SCROLLINFO);
	si.fMask = SIF_RANGE | SIF_PAGE;
	si.nMin = 0;
	si.nMax = (int) (areaWidthLONG(data) - 1);		// the max point is inclusive, so we have to pass in the last valid value, not the first invalid one (see http://blogs.msdn.com/b/oldnewthing/archive/2003/07/31/54601.aspx); if we don't, we get weird things like the scrollbar sometimes showing one extra scroll position at the end that you can never scroll to
	si.nPage = (UINT) cwid;
	SetScrollInfo(hwnd, SB_HORZ, &si, TRUE);		// redraw the scroll bar

	// MSDN sample code does this a second time; let's do it too to be safe
	ZeroMemory(&si, sizeof (SCROLLINFO));
	si.cbSize = sizeof (SCROLLINFO);
	si.fMask = SIF_RANGE | SIF_PAGE;
	si.nMin = 0;
	si.nMax = (int) (areaHeightLONG(data) - 1);
	si.nPage = (UINT) cht;
	SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
}

void repaintArea(HWND hwnd)
{
	// TODO second argument pointer?
	// 0 - the whole area; TRUE - have windows erase if possible
	if (InvalidateRect(hwnd, 0, TRUE) == 0)
		xpanic("error flagging Area as needing repainting after event", GetLastError());
	if (UpdateWindow(hwnd) == 0)
		xpanic("error repainting Area after event", GetLastError());
}

void areaMouseEvent(HWND hwnd, void *data, DWORD button, BOOL up, LPARAM lParam)
{
	int xpos, ypos;

	// mouse coordinates are relative to control; make them relative to Area
	getScrollPos(hwnd, &xpos, &ypos);
	xpos += GET_X_LPARAM(lParam);
	ypos += GET_Y_LPARAM(lParam);
	finishAreaMouseEvent(data, button, up, xpos, ypos);
}

static LRESULT CALLBACK areaWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	void *data;
	DWORD which;

	data = (void *) GetWindowLongPtrW(hwnd, GWLP_USERDATA);
	if (data == NULL) {
		/* the lpParam is available during WM_NCCREATE and WM_CREATE */
		if (uMsg == WM_NCCREATE) {
			storelpParam(hwnd, lParam);
			data = (void *) GetWindowLongPtrW(hwnd, GWLP_USERDATA);
			storeAreaHWND(data, hwnd);
		}
		/* act as if we're not ready yet, even during WM_NCCREATE (nothing important to the switch statement below happens here anyway) */
		return DefWindowProcW(hwnd, uMsg, wParam, lParam);
	}

	switch (uMsg) {
	case WM_PAINT:
		paintArea(hwnd, data);
		return 0;
	case WM_ERASEBKGND:
		// don't draw a background; we'll do so when painting
		// this is to make things flicker-free; see http://msdn.microsoft.com/en-us/library/ms969905.aspx
		return 1;
	case WM_HSCROLL:
		scrollArea(hwnd, data, wParam, SB_HORZ);
		return 0;
	case WM_VSCROLL:
		scrollArea(hwnd, data, wParam, SB_VERT);
		return 0;
	case WM_SIZE:
		adjustAreaScrollbars(hwnd, data);
		return 0;
	case WM_ACTIVATE:
		// don't keep the double-click timer running if the user switched programs in between clicks
		areaResetClickCounter(data);
		return 0;
	case WM_MOUSEACTIVATE:
		// this happens on every mouse click (apparently), so DON'T reset the click counter, otherwise it will always be reset (not an issue, as MSDN says WM_ACTIVATE is sent alongside WM_MOUSEACTIVATE when necessary)
		// transfer keyboard focus to our Area on an activating click
		// (see http://www.catch22.net/tuts/custom-controls)
		// don't bother checking SetFocus()'s error; see http://stackoverflow.com/questions/24073695/winapi-can-setfocus-return-null-without-an-error-because-thats-what-im-see/24074912#24074912
		SetFocus(hwnd);
		// and don't eat the click, as we want to handle clicks that switch into Windows with Areas from other windows
		return MA_ACTIVATE;
	case WM_MOUSEMOVE:
		areaMouseEvent(hwnd, data, 0, FALSE, lParam);
		return 0;
	case WM_LBUTTONDOWN:
		areaMouseEvent(hwnd, data, 1, FALSE, lParam);
		return 0;
	case WM_LBUTTONUP:
		areaMouseEvent(hwnd, data, 1, TRUE, lParam);
		return 0;
	case WM_MBUTTONDOWN:
		areaMouseEvent(hwnd, data, 2, FALSE, lParam);
		return 0;
	case WM_MBUTTONUP:
		areaMouseEvent(hwnd, data, 2, TRUE, lParam);
		return 0;
	case WM_RBUTTONDOWN:
		areaMouseEvent(hwnd, data, 3, FALSE, lParam);
		return 0;
	case WM_RBUTTONUP:
		areaMouseEvent(hwnd, data, 3, TRUE, lParam);
		return 0;
	case WM_XBUTTONDOWN:
		// values start at 1; we want them to start at 4
		which = (DWORD) GET_XBUTTON_WPARAM(wParam) + 3;
		areaMouseEvent(hwnd, data, which, FALSE, lParam);
		return TRUE;		// XBUTTON messages are different!
	case WM_XBUTTONUP:
		which = (DWORD) GET_XBUTTON_WPARAM(wParam) + 3;
		areaMouseEvent(hwnd, data, which, TRUE, lParam);
		return TRUE;
	case WM_KEYDOWN:
		areaKeyEvent(data, FALSE, wParam, lParam);
		return 0;
	case WM_KEYUP:
		areaKeyEvent(data, TRUE, wParam, lParam);
		return 0;
	// Alt+[anything] and F10 send these instead and require us to return to DefWindowProc() so global keystrokes such as Alt+Tab can be processed
	case WM_SYSKEYDOWN:
		areaKeyEvent(data, FALSE, wParam, lParam);
		return DefWindowProcW(hwnd, uMsg, wParam, lParam);
	case WM_SYSKEYUP:
		areaKeyEvent(data, TRUE, wParam, lParam);
		return DefWindowProcW(hwnd, uMsg, wParam, lParam);
	case msgAreaSizeChanged:
		adjustAreaScrollbars(hwnd, data);
		repaintArea(hwnd);		// this calls for an update
		return 0;
	case msgAreaRepaintAll:
		repaintArea(hwnd);
		return 0;
	default:
		return DefWindowProcW(hwnd, uMsg, wParam, lParam);
	}
	xmissedmsg("Area", "areaWndProc()", uMsg);
	return 0;			/* unreached */
}

DWORD makeAreaWindowClass(char **errmsg)
{
	WNDCLASSW wc;

	ZeroMemory(&wc, sizeof (WNDCLASSW));
	wc.style = CS_HREDRAW | CS_VREDRAW;			// no CS_DBLCLKS because do that manually
	wc.lpszClassName = areaWindowClass;
	wc.lpfnWndProc = areaWndProc;
	wc.hInstance = hInstance;
	wc.hIcon = hDefaultIcon;
	wc.hCursor = hArrowCursor,
	wc.hbrBackground = NULL;		// no brush; we handle WM_ERASEBKGND
	if (RegisterClassW(&wc) == 0) {
		*errmsg = "error registering Area window class";
		return GetLastError();
	}
	return 0;
}

HWND newArea(void *data)
{
	HWND hwnd;

	hwnd = CreateWindowExW(
		0,
		areaWindowClass, L"",
		WS_HSCROLL | WS_VSCROLL | WS_CHILD | WS_VISIBLE,
		CW_USEDEFAULT, CW_USEDEFAULT,
		100, 100,
		msgwin, NULL, hInstance, data);
	if (hwnd == NULL)
		xpanic("container creation failed", GetLastError());
	return hwnd;
}
