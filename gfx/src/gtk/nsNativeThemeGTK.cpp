/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2002
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *  Brian Ryner <bryner@brianryner.com>  (Original Author)
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include "nsNativeThemeGTK.h"
#include "nsThemeConstants.h"
#include "nsDrawingSurfaceGTK.h"
#include "nsDeviceContextGTK.h"
#include "gtkdrawing.h"

#include "nsIObserverService.h"
#include "nsIServiceManager.h"
#include "nsIFrame.h"
#include "nsIPresShell.h"
#include "nsIDocument.h"
#include "nsIContent.h"
#include "nsIEventStateManager.h"
#include "nsIViewManager.h"
#include "nsINameSpaceManager.h"
#include "nsILookAndFeel.h"
#include "nsIDeviceContext.h"
#include "nsTransform2D.h"
#include "nsIMenuFrame.h"
#include "nsIMenuParent.h"
#include "prlink.h"

#include <gdk/gdkprivate.h>

#include <gdk/gdkx.h>

NS_IMPL_ISUPPORTS2(nsNativeThemeGTK, nsITheme, nsIObserver)

static int gLastXError;

nsNativeThemeGTK::nsNativeThemeGTK()
{
  if (moz_gtk_init() != MOZ_GTK_SUCCESS) {
    memset(mDisabledWidgetTypes, 0xff, sizeof(mDisabledWidgetTypes));
    return;
  }

  // We have to call moz_gtk_shutdown before the event loop stops running.
  nsCOMPtr<nsIObserverService> obsServ =
    do_GetService("@mozilla.org/observer-service;1");
  obsServ->AddObserver(this, "xpcom-shutdown", PR_FALSE);

  mInputCheckedAtom = do_GetAtom("_moz-input-checked");
  mInputAtom = do_GetAtom("input");
  mCurPosAtom = do_GetAtom("curpos");
  mMaxPosAtom = do_GetAtom("maxpos");
  mMenuActiveAtom = do_GetAtom("_moz-menuactive");

  memset(mDisabledWidgetTypes, 0, sizeof(mDisabledWidgetTypes));
  memset(mSafeWidgetStates, 0, sizeof(mSafeWidgetStates));

#ifdef MOZ_WIDGET_GTK
  // Look up the symbol for gtk_style_get_prop_experimental
  PRLibrary* gtkLibrary;
  PRFuncPtr stylePropFunc = PR_FindFunctionSymbolAndLibrary("gtk_style_get_prop_experimental", &gtkLibrary);
  if (stylePropFunc) {
    moz_gtk_enable_style_props((style_prop_t) stylePropFunc);
    PR_UnloadLibrary(gtkLibrary);
  }
#endif
}

nsNativeThemeGTK::~nsNativeThemeGTK() {
}

NS_IMETHODIMP
nsNativeThemeGTK::Observe(nsISupports *aSubject, const char *aTopic,
                          const PRUnichar *aData)
{
  if (!nsCRT::strcmp(aTopic, "xpcom-shutdown")) {
    moz_gtk_shutdown();
  } else {
    NS_NOTREACHED("unexpected topic");
    return NS_ERROR_UNEXPECTED;
  }

  return NS_OK;
}

void
nsNativeThemeGTK::RefreshWidgetWindow(nsIFrame* aFrame)
{
  nsIPresShell *shell = GetPresShell(aFrame);
  if (!shell)
    return;

  nsIViewManager* vm = shell->GetViewManager();
  if (!vm)
    return;
 
  vm->UpdateAllViews(NS_VMREFRESH_NO_SYNC);
}

static PRBool IsWidgetTypeDisabled(PRUint8* aDisabledVector, PRUint8 aWidgetType) {
  return aDisabledVector[aWidgetType >> 3] & (1 << (aWidgetType & 7));
}

static void SetWidgetTypeDisabled(PRUint8* aDisabledVector, PRUint8 aWidgetType) {
  aDisabledVector[aWidgetType >> 3] |= (1 << (aWidgetType & 7));
}

static inline PRUint16
GetWidgetStateKey(PRUint8 aWidgetType, GtkWidgetState *aWidgetState)
{
  return (aWidgetState->active |
          aWidgetState->focused << 1 |
          aWidgetState->inHover << 2 |
          aWidgetState->disabled << 3 |
          aWidgetState->isDefault << 4 |
          aWidgetType << 5);
}

static PRBool IsWidgetStateSafe(PRUint8* aSafeVector,
                                PRUint8 aWidgetType,
                                GtkWidgetState *aWidgetState)
{
  PRUint8 key = GetWidgetStateKey(aWidgetType, aWidgetState);
  return aSafeVector[key >> 3] & (1 << (key & 7));
}

static void SetWidgetStateSafe(PRUint8 *aSafeVector,
                               PRUint8 aWidgetType,
                               GtkWidgetState *aWidgetState)
{
  PRUint8 key = GetWidgetStateKey(aWidgetType, aWidgetState);
  aSafeVector[key >> 3] |= (1 << (key & 7));
}

PRBool
nsNativeThemeGTK::GetGtkWidgetAndState(PRUint8 aWidgetType, nsIFrame* aFrame,
                                       GtkThemeWidgetType& aGtkWidgetType,
                                       GtkWidgetState* aState,
                                       gint* aWidgetFlags)
{
  if (aState) {
    if (!aFrame) {
      // reset the entire struct to zero
      memset(aState, 0, sizeof(GtkWidgetState));
    } else {

      // for dropdown textfields, look at the parent frame (textbox or menulist)
      if (aWidgetType == NS_THEME_DROPDOWN_TEXTFIELD)
        aFrame = aFrame->GetParent();

      // For XUL checkboxes and radio buttons, the state of the parent
      // determines our state.
      if (aWidgetType == NS_THEME_CHECKBOX || aWidgetType == NS_THEME_RADIO ||
          aWidgetType == NS_THEME_CHECKBOX_LABEL ||
          aWidgetType == NS_THEME_RADIO_LABEL) {
        if (aWidgetFlags) {
          nsIAtom* atom = nsnull;

          if (aFrame) {
            nsIContent* content = aFrame->GetContent();
            if (content->IsContentOfType(nsIContent::eXUL)) {
              aFrame = aFrame->GetParent();
              if (aWidgetType == NS_THEME_CHECKBOX_LABEL ||
                  aWidgetType == NS_THEME_RADIO_LABEL)
                aFrame = aFrame->GetParent();
            }
            else if (content->Tag() == mInputAtom)
              atom = mInputCheckedAtom;
          }

          if (!atom)
            atom = (aWidgetType == NS_THEME_CHECKBOX || aWidgetType == NS_THEME_CHECKBOX_LABEL) ? mCheckedAtom : mSelectedAtom;
          *aWidgetFlags = CheckBooleanAttr(aFrame, atom);
        }
      }

      PRInt32 eventState = GetContentState(aFrame, aWidgetType);

      aState->disabled = IsDisabled(aFrame);
      aState->active  = (eventState & NS_EVENT_STATE_ACTIVE) == NS_EVENT_STATE_ACTIVE;
      aState->focused = (eventState & NS_EVENT_STATE_FOCUS) == NS_EVENT_STATE_FOCUS;
      aState->inHover = (eventState & NS_EVENT_STATE_HOVER) == NS_EVENT_STATE_HOVER;
      aState->isDefault = FALSE; // XXX fix me
      aState->canDefault = FALSE; // XXX fix me

      // For these widget types, some element (either a child or parent)
      // actually has element focus, so we check the focused attribute
      // to see whether to draw in the focused state.
      if (aWidgetType == NS_THEME_TEXTFIELD ||
          aWidgetType == NS_THEME_DROPDOWN_TEXTFIELD ||
          aWidgetType == NS_THEME_RADIO_CONTAINER ||
          aWidgetType == NS_THEME_RADIO_LABEL ||
          aWidgetType == NS_THEME_RADIO) {
        aState->focused = IsFocused(aFrame);
      }

      if (aWidgetType == NS_THEME_SCROLLBAR_THUMB_VERTICAL ||
          aWidgetType == NS_THEME_SCROLLBAR_THUMB_HORIZONTAL) {
        // for scrollbars we need to go up two to go from the thumb to
        // the slider to the actual scrollbar object
        nsIFrame *tmpFrame = aFrame->GetParent()->GetParent();

        aState->curpos = CheckIntAttr(tmpFrame, mCurPosAtom);
        aState->maxpos = CheckIntAttr(tmpFrame, mMaxPosAtom);
      }

      // menu item state is determined by the attribute "_moz-menuactive",
      // and not by the mouse hovering (accessibility).  as a special case,
      // menus which are children of a menu bar are only marked as prelight
      // if they are open, not on normal hover.

      if (aWidgetType == NS_THEME_MENUITEM ||
          aWidgetType == NS_THEME_CHECKMENUITEM ||
          aWidgetType == NS_THEME_RADIOMENUITEM) {
        PRBool isTopLevel = PR_FALSE;
        nsIMenuFrame *menuFrame;
        CallQueryInterface(aFrame, &menuFrame);

        if (menuFrame) {
          nsIMenuParent *menuParent = menuFrame->GetMenuParent();
          if (menuParent)
            menuParent->IsMenuBar(isTopLevel);
        }

        if (isTopLevel) {
          PRBool isOpen;
          menuFrame->MenuIsOpen(isOpen);
          aState->inHover = isOpen;
        } else {
          aState->inHover = CheckBooleanAttr(aFrame, mMenuActiveAtom);
        }

        aState->active = FALSE;
        
        if (aWidgetType == NS_THEME_CHECKMENUITEM ||
            aWidgetType == NS_THEME_RADIOMENUITEM) {
          if (aFrame) {
            nsAutoString attr;
            nsresult res = aFrame->GetContent()->GetAttr(kNameSpaceID_None, mCheckedAtom, attr);
            if (res == NS_CONTENT_ATTR_NO_VALUE ||
               (res != NS_CONTENT_ATTR_NOT_THERE && attr.IsEmpty()))
              *aWidgetFlags = FALSE;
            else
              *aWidgetFlags = attr.EqualsIgnoreCase("true");
          } else {
            *aWidgetFlags = FALSE;
          }
        }
      }
    }
  }

  switch (aWidgetType) {
  case NS_THEME_BUTTON:
  case NS_THEME_TOOLBAR_BUTTON:
  case NS_THEME_TOOLBAR_DUAL_BUTTON:
    if (aWidgetFlags)
      *aWidgetFlags = (aWidgetType == NS_THEME_BUTTON) ? GTK_RELIEF_NORMAL : GTK_RELIEF_NONE;
    aGtkWidgetType = MOZ_GTK_BUTTON;
    break;
  case NS_THEME_CHECKBOX:
  case NS_THEME_RADIO:
    aGtkWidgetType = (aWidgetType == NS_THEME_RADIO) ? MOZ_GTK_RADIOBUTTON : MOZ_GTK_CHECKBUTTON;
    break;
  case NS_THEME_SCROLLBAR_BUTTON_UP:
  case NS_THEME_SCROLLBAR_BUTTON_DOWN:
  case NS_THEME_SCROLLBAR_BUTTON_LEFT:
  case NS_THEME_SCROLLBAR_BUTTON_RIGHT:
    if (aWidgetFlags)
      *aWidgetFlags = GtkArrowType(aWidgetType - NS_THEME_SCROLLBAR_BUTTON_UP);
    aGtkWidgetType = MOZ_GTK_SCROLLBAR_BUTTON;
    break;
  case NS_THEME_SCROLLBAR_TRACK_VERTICAL:
    aGtkWidgetType = MOZ_GTK_SCROLLBAR_TRACK_VERTICAL;
    break;
  case NS_THEME_SCROLLBAR_TRACK_HORIZONTAL:
    aGtkWidgetType = MOZ_GTK_SCROLLBAR_TRACK_HORIZONTAL;
    break;
  case NS_THEME_SCROLLBAR_THUMB_VERTICAL:
    aGtkWidgetType = MOZ_GTK_SCROLLBAR_THUMB_VERTICAL;
    break;
  case NS_THEME_SCROLLBAR_THUMB_HORIZONTAL:
    aGtkWidgetType = MOZ_GTK_SCROLLBAR_THUMB_HORIZONTAL;
    break;
  case NS_THEME_TOOLBAR_GRIPPER:
    aGtkWidgetType = MOZ_GTK_GRIPPER;
    break;
  case NS_THEME_TEXTFIELD:
  case NS_THEME_DROPDOWN_TEXTFIELD:
    aGtkWidgetType = MOZ_GTK_ENTRY;
    break;
  case NS_THEME_DROPDOWN:
    aGtkWidgetType = MOZ_GTK_DROPDOWN;
    break;
  case NS_THEME_DROPDOWN_TEXT:
    return PR_FALSE; // nothing to do, but prevents the bg from being drawn
  case NS_THEME_DROPDOWN_BUTTON:
    aGtkWidgetType = MOZ_GTK_DROPDOWN_ARROW;
    break;
  case NS_THEME_CHECKBOX_CONTAINER:
    aGtkWidgetType = MOZ_GTK_CHECKBUTTON_CONTAINER;
    break;
  case NS_THEME_RADIO_CONTAINER:
    aGtkWidgetType = MOZ_GTK_RADIOBUTTON_CONTAINER;
    break;
  case NS_THEME_CHECKBOX_LABEL:
    aGtkWidgetType = MOZ_GTK_CHECKBUTTON_LABEL;
    break;
  case NS_THEME_RADIO_LABEL:
    aGtkWidgetType = MOZ_GTK_RADIOBUTTON_LABEL;
    break;
  case NS_THEME_TOOLBAR:
    aGtkWidgetType = MOZ_GTK_TOOLBAR;
    break;
  case NS_THEME_TOOLTIP:
    aGtkWidgetType = MOZ_GTK_TOOLTIP;
    break;
  case NS_THEME_STATUSBAR_PANEL:
    aGtkWidgetType = MOZ_GTK_FRAME;
    break;
  case NS_THEME_PROGRESSBAR:
  case NS_THEME_PROGRESSBAR_VERTICAL:
    aGtkWidgetType = MOZ_GTK_PROGRESSBAR;
    break;
  case NS_THEME_PROGRESSBAR_CHUNK:
  case NS_THEME_PROGRESSBAR_CHUNK_VERTICAL:
    aGtkWidgetType = MOZ_GTK_PROGRESS_CHUNK;
    break;
  case NS_THEME_TAB_PANELS:
    aGtkWidgetType = MOZ_GTK_TABPANELS;
    break;
  case NS_THEME_TAB:
  case NS_THEME_TAB_LEFT_EDGE:
  case NS_THEME_TAB_RIGHT_EDGE:
    {
      if (aWidgetFlags) {
        *aWidgetFlags = 0;

        if (aWidgetType == NS_THEME_TAB &&
            CheckBooleanAttr(aFrame, mSelectedAtom))
          *aWidgetFlags |= MOZ_GTK_TAB_SELECTED;
        else if (aWidgetType == NS_THEME_TAB_LEFT_EDGE)
          *aWidgetFlags |= MOZ_GTK_TAB_BEFORE_SELECTED;

        if (aFrame->GetContent()->HasAttr(kNameSpaceID_None, mFirstTabAtom))
          *aWidgetFlags |= MOZ_GTK_TAB_FIRST;
      }

      aGtkWidgetType = MOZ_GTK_TAB;
    }
    break;
  case NS_THEME_MENUBAR:
    aGtkWidgetType = MOZ_GTK_MENUBAR;
    break;
  case NS_THEME_MENUPOPUP:
    aGtkWidgetType = MOZ_GTK_MENUPOPUP;
    break;
  case NS_THEME_MENUITEM:
    aGtkWidgetType = MOZ_GTK_MENUITEM;
    break;
  case NS_THEME_CHECKMENUITEM:
    aGtkWidgetType = MOZ_GTK_CHECKMENUITEM;
    break;
  case NS_THEME_RADIOMENUITEM:
    aGtkWidgetType = MOZ_GTK_RADIOMENUITEM;
    break;
  case NS_THEME_WINDOW:
  case NS_THEME_DIALOG:
    aGtkWidgetType = MOZ_GTK_WINDOW;
    break;
  default:
    return PR_FALSE;
  }

  return PR_TRUE;
}

static int
NativeThemeErrorHandler(Display* dpy, XErrorEvent* error) {
  gLastXError = error->error_code;
  return 0;
}

NS_IMETHODIMP
nsNativeThemeGTK::DrawWidgetBackground(nsIRenderingContext* aContext,
                                       nsIFrame* aFrame,
                                       PRUint8 aWidgetType,
                                       const nsRect& aRect,
                                       const nsRect& aClipRect)
{
  GtkWidgetState state;
  GtkThemeWidgetType gtkWidgetType;
  gint flags;
  if (!GetGtkWidgetAndState(aWidgetType, aFrame, gtkWidgetType, &state,
                            &flags))
    return NS_OK;

  nsDrawingSurfaceGTK* surface;
  aContext->GetDrawingSurface((nsIDrawingSurface**)&surface);
  NS_ENSURE_TRUE(surface, NS_ERROR_FAILURE);
  GdkWindow* window = (GdkWindow*) surface->GetDrawable();
  NS_ENSURE_TRUE(window, NS_ERROR_FAILURE);

  nsTransform2D* transformMatrix;
  aContext->GetCurrentTransform(transformMatrix);

  nsRect tr(aRect);
  transformMatrix->TransformCoord(&tr.x, &tr.y, &tr.width, &tr.height);
  GdkRectangle gdk_rect = {tr.x, tr.y, tr.width, tr.height};

  nsRect cr(aClipRect);
  transformMatrix->TransformCoord(&cr.x, &cr.y, &cr.width, &cr.height);
  GdkRectangle gdk_clip = {cr.x, cr.y, cr.width, cr.height};

  NS_ASSERTION(!IsWidgetTypeDisabled(mDisabledWidgetTypes, aWidgetType),
               "Trying to render an unsafe widget!");

  PRBool safeState = IsWidgetStateSafe(mSafeWidgetStates, aWidgetType, &state);
  XErrorHandler oldHandler = nsnull;
  if (!safeState) {
    gLastXError = 0;
    oldHandler = XSetErrorHandler(NativeThemeErrorHandler);
  }

  moz_gtk_widget_paint(gtkWidgetType, window, &gdk_rect, &gdk_clip, &state,
                       flags);

  if (!safeState) {
    gdk_flush();
    XSetErrorHandler(oldHandler);

    if (gLastXError) {
#ifdef DEBUG
      printf("GTK theme failed for widget type %d, error was %d, state was "
             "[active=%d,focused=%d,inHover=%d,disabled=%d]\n",
             aWidgetType, gLastXError, state.active, state.focused,
             state.inHover, state.disabled);
#endif
      NS_WARNING("GTK theme failed; disabling unsafe widget");
      SetWidgetTypeDisabled(mDisabledWidgetTypes, aWidgetType);
      // force refresh of the window, because the widget was not
      // successfully drawn it must be redrawn using the default look
      RefreshWidgetWindow(aFrame);
    } else {
      SetWidgetStateSafe(mSafeWidgetStates, aWidgetType, &state);
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
nsNativeThemeGTK::GetWidgetBorder(nsIDeviceContext* aContext, nsIFrame* aFrame,
                                  PRUint8 aWidgetType, nsMargin* aResult)
{
  aResult->top = aResult->left = 0;
  switch (aWidgetType) {
  case NS_THEME_SCROLLBAR_TRACK_VERTICAL:
  case NS_THEME_SCROLLBAR_TRACK_HORIZONTAL:
    {
      MozGtkScrollbarMetrics metrics;
      moz_gtk_get_scrollbar_metrics(&metrics);
      aResult->top = aResult->left = metrics.trough_border;
    }
    break;
  case NS_THEME_TOOLBOX:
    // gtk has no toolbox equivalent.  So, although we map toolbox to
    // gtk's 'toolbar' for purposes of painting the widget background,
    // we don't use the toolbar border for toolbox.
    break;
  case NS_THEME_TOOLBAR_DUAL_BUTTON:
    // TOOLBAR_DUAL_BUTTON is an interesting case.  We want a border to draw
    // around the entire button + dropdown, and also an inner border if you're
    // over the button part.  But, we want the inner button to be right up
    // against the edge of the outer button so that the borders overlap.
    // To make this happen, we draw a button border for the outer button,
    // but don't reserve any space for it.
    break;
  default:
    {
      GtkThemeWidgetType gtkWidgetType;
      if (GetGtkWidgetAndState(aWidgetType, aFrame, gtkWidgetType, nsnull,
                               nsnull))
        moz_gtk_get_widget_border(gtkWidgetType, &aResult->left,
                                  &aResult->top);
    }
  }

  aResult->right = aResult->left;
  aResult->bottom = aResult->top;

  return NS_OK;
}

PRBool
nsNativeThemeGTK::GetWidgetPadding(nsIDeviceContext* aContext,
                                   nsIFrame* aFrame, PRUint8 aWidgetType,
                                   nsMargin* aResult)
{
  if (aWidgetType == NS_THEME_BUTTON_FOCUS ||
      aWidgetType == NS_THEME_TOOLBAR_BUTTON ||
      aWidgetType == NS_THEME_TOOLBAR_DUAL_BUTTON) {
    aResult->SizeTo(0, 0, 0, 0);
    return PR_TRUE;
  }

  return PR_FALSE;
}

PRBool
nsNativeThemeGTK::GetWidgetOverflow(nsIDeviceContext* aContext,
                                    nsIFrame* aFrame, PRUint8 aWidgetType,
                                    nsRect* aResult)
{
  nsIntMargin extraSize(0,0,0,0);
  // Allow an extra one pixel above and below the thumb for certain
  // GTK2 themes (Ximian Industrial, Bluecurve, Misty, at least);
  // see moz_gtk_scrollbar_thumb_paint in gtk2drawing.c
  switch (aWidgetType) {
  case NS_THEME_SCROLLBAR_THUMB_VERTICAL:
    extraSize.top = extraSize.bottom = 1;
    break;
  case NS_THEME_SCROLLBAR_THUMB_HORIZONTAL:
    extraSize.left = extraSize.right = 1;
    break;
  default:
    return PR_FALSE;
  }

  float p2t = aContext->DevUnitsToAppUnits();
  nsMargin m(NSIntPixelsToTwips(extraSize.left, p2t),
             NSIntPixelsToTwips(extraSize.top, p2t),
             NSIntPixelsToTwips(extraSize.right, p2t),
             NSIntPixelsToTwips(extraSize.bottom, p2t));
  nsRect r(nsPoint(0, 0), aFrame->GetSize());
  r.Inflate(m);
  *aResult = r;
  return PR_TRUE;
}

NS_IMETHODIMP
nsNativeThemeGTK::GetMinimumWidgetSize(nsIRenderingContext* aContext,
                                       nsIFrame* aFrame, PRUint8 aWidgetType,
                                       nsSize* aResult, PRBool* aIsOverridable)
{
  aResult->width = aResult->height = 0;
  *aIsOverridable = PR_TRUE;

  switch (aWidgetType) {
    case NS_THEME_SCROLLBAR_BUTTON_UP:
    case NS_THEME_SCROLLBAR_BUTTON_DOWN:
      {
        MozGtkScrollbarMetrics metrics;
        moz_gtk_get_scrollbar_metrics(&metrics);

        aResult->width = metrics.slider_width;
        aResult->height = metrics.stepper_size;
        *aIsOverridable = PR_FALSE;
      }
      break;
    case NS_THEME_SCROLLBAR_BUTTON_LEFT:
    case NS_THEME_SCROLLBAR_BUTTON_RIGHT:
      {
        MozGtkScrollbarMetrics metrics;
        moz_gtk_get_scrollbar_metrics(&metrics);

        aResult->width = metrics.stepper_size;
        aResult->height = metrics.slider_width;
        *aIsOverridable = PR_FALSE;
      }
      break;
    case NS_THEME_SCROLLBAR_THUMB_VERTICAL:
    case NS_THEME_SCROLLBAR_THUMB_HORIZONTAL:
      {
        MozGtkScrollbarMetrics metrics;
        moz_gtk_get_scrollbar_metrics(&metrics);

        if (aWidgetType == NS_THEME_SCROLLBAR_THUMB_VERTICAL) {
          aResult->width = metrics.slider_width;
          aResult->height = metrics.min_slider_size;
        } else {
          aResult->width = metrics.min_slider_size;
          aResult->height = metrics.slider_width;
        }

        *aIsOverridable = PR_FALSE;
      }
      break;
  case NS_THEME_DROPDOWN_BUTTON:
    {
      moz_gtk_get_dropdown_arrow_size(&aResult->width, &aResult->height);
      *aIsOverridable = PR_FALSE;
    }
    break;
  case NS_THEME_CHECKBOX:
  case NS_THEME_RADIO:
    {
      gint indicator_size, indicator_spacing;

      if (aWidgetType == NS_THEME_CHECKBOX) {
        moz_gtk_checkbox_get_metrics(&indicator_size, &indicator_spacing);
      } else {
        moz_gtk_radio_get_metrics(&indicator_size, &indicator_spacing);
      }

      // Include space for the indicator and the padding around it.
      aResult->width = indicator_size + 3 * indicator_spacing;
      aResult->height = indicator_size + 2 * indicator_spacing;
      *aIsOverridable = PR_FALSE;
    }
    break;

  case NS_THEME_CHECKBOX_CONTAINER:
  case NS_THEME_RADIO_CONTAINER:
  case NS_THEME_CHECKBOX_LABEL:
  case NS_THEME_RADIO_LABEL:
  case NS_THEME_BUTTON:
  case NS_THEME_TOOLBAR_BUTTON:
    {
      // Just include our border, and let the box code augment the size.

      nsCOMPtr<nsIDeviceContext> dc;
      aContext->GetDeviceContext(*getter_AddRefs(dc));

      nsMargin border;
      nsNativeThemeGTK::GetWidgetBorder(dc, aFrame, aWidgetType, &border);
      aResult->width = border.left + border.right;
      aResult->height = border.top + border.bottom;
    }
    break;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsNativeThemeGTK::WidgetStateChanged(nsIFrame* aFrame, PRUint8 aWidgetType, 
                                     nsIAtom* aAttribute, PRBool* aShouldRepaint)
{
  // Some widget types just never change state.
  if (aWidgetType == NS_THEME_TOOLBOX ||
      aWidgetType == NS_THEME_TOOLBAR ||
      aWidgetType == NS_THEME_STATUSBAR ||
      aWidgetType == NS_THEME_STATUSBAR_PANEL ||
      aWidgetType == NS_THEME_STATUSBAR_RESIZER_PANEL ||
      aWidgetType == NS_THEME_PROGRESSBAR_CHUNK ||
      aWidgetType == NS_THEME_PROGRESSBAR_CHUNK_VERTICAL ||
      aWidgetType == NS_THEME_PROGRESSBAR ||
      aWidgetType == NS_THEME_PROGRESSBAR_VERTICAL ||
      aWidgetType == NS_THEME_MENUBAR ||
      aWidgetType == NS_THEME_MENUPOPUP ||
      aWidgetType == NS_THEME_TOOLTIP ||
      aWidgetType == NS_THEME_WINDOW ||
      aWidgetType == NS_THEME_DIALOG) {
    *aShouldRepaint = PR_FALSE;
    return NS_OK;
  }

  // XXXdwh Not sure what can really be done here.  Can at least guess for
  // specific widgets that they're highly unlikely to have certain states.
  // For example, a toolbar doesn't care about any states.
  if (!aAttribute) {
    // Hover/focus/active changed.  Always repaint.
    *aShouldRepaint = PR_TRUE;
  }
  else {
    // Check the attribute to see if it's relevant.  
    // disabled, checked, dlgtype, default, etc.
    *aShouldRepaint = PR_FALSE;
    if (aAttribute == mDisabledAtom || aAttribute == mCheckedAtom ||
        aAttribute == mSelectedAtom || aAttribute == mFocusedAtom ||
        aAttribute == mMenuActiveAtom)
      *aShouldRepaint = PR_TRUE;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsNativeThemeGTK::ThemeChanged()
{
  nsDeviceContextGTK::ClearCachedSystemFonts();

  memset(mDisabledWidgetTypes, 0, sizeof(mDisabledWidgetTypes));
  return NS_OK;
}

NS_IMETHODIMP_(PRBool)
nsNativeThemeGTK::ThemeSupportsWidget(nsPresContext* aPresContext,
                                      nsIFrame* aFrame,
                                      PRUint8 aWidgetType)
{
  if (aFrame) {
    // For now don't support HTML.
    if (aFrame->GetContent()->IsContentOfType(nsIContent::eHTML))
      return PR_FALSE;
  }

  if (IsWidgetTypeDisabled(mDisabledWidgetTypes, aWidgetType))
    return PR_FALSE;

  switch (aWidgetType) {
  case NS_THEME_BUTTON:
  case NS_THEME_BUTTON_FOCUS:
  case NS_THEME_RADIO:
  case NS_THEME_CHECKBOX:
  case NS_THEME_TOOLBOX: // N/A
  case NS_THEME_TOOLBAR:
  case NS_THEME_TOOLBAR_BUTTON:
  case NS_THEME_TOOLBAR_DUAL_BUTTON: // so we can override the border with 0
    // case NS_THEME_TOOLBAR_DUAL_BUTTON_DROPDOWN:
    // case NS_THEME_TOOLBAR_SEPARATOR:
  case NS_THEME_TOOLBAR_GRIPPER:
  case NS_THEME_STATUSBAR:
  case NS_THEME_STATUSBAR_PANEL:
    // case NS_THEME_RESIZER:  (n/a for gtk)
    // case NS_THEME_LISTBOX:
    // case NS_THEME_LISTBOX_LISTITEM:
    // case NS_THEME_TREEVIEW:
    // case NS_THEME_TREEVIEW_TREEITEM:
    // case NS_THEME_TREEVIEW_TWISTY:
    // case NS_THEME_TREEVIEW_LINE:
    // case NS_THEME_TREEVIEW_HEADER:
    // case NS_THEME_TREEVIEW_HEADER_CELL:
    // case NS_THEME_TREEVIEW_HEADER_SORTARROW:
    // case NS_THEME_TREEVIEW_TWISTY_OPEN:
    case NS_THEME_PROGRESSBAR:
    case NS_THEME_PROGRESSBAR_CHUNK:
    case NS_THEME_PROGRESSBAR_VERTICAL:
    case NS_THEME_PROGRESSBAR_CHUNK_VERTICAL:
    case NS_THEME_TAB:
    // case NS_THEME_TAB_PANEL:
    case NS_THEME_TAB_LEFT_EDGE:
    case NS_THEME_TAB_RIGHT_EDGE:
    case NS_THEME_TAB_PANELS:
  case NS_THEME_TOOLTIP:
    // case NS_THEME_SPINNER:
    // case NS_THEME_SPINNER_UP_BUTTON:
    // case NS_THEME_SPINNER_DOWN_BUTTON:
    // case NS_THEME_SCROLLBAR:  (n/a for gtk)
  case NS_THEME_SCROLLBAR_BUTTON_UP:
  case NS_THEME_SCROLLBAR_BUTTON_DOWN:
  case NS_THEME_SCROLLBAR_BUTTON_LEFT:
  case NS_THEME_SCROLLBAR_BUTTON_RIGHT:
  case NS_THEME_SCROLLBAR_TRACK_HORIZONTAL:
  case NS_THEME_SCROLLBAR_TRACK_VERTICAL:
  case NS_THEME_SCROLLBAR_THUMB_HORIZONTAL:
  case NS_THEME_SCROLLBAR_THUMB_VERTICAL:
    // case NS_THEME_SCROLLBAR_GRIPPER_HORIZONTAL:  (n/a for gtk)
    // case NS_THEME_SCROLLBAR_GRIPPER_VERTICAL:  (n/a for gtk)
  case NS_THEME_TEXTFIELD:
    // case NS_THEME_TEXTFIELD_CARET:
  case NS_THEME_DROPDOWN_BUTTON:
  case NS_THEME_DROPDOWN_TEXTFIELD:
    // case NS_THEME_SLIDER:
    // case NS_THEME_SLIDER_THUMB:
    // case NS_THEME_SLIDER_THUMB_START:
    // case NS_THEME_SLIDER_THUMB_END:
    // case NS_THEME_SLIDER_TICK:
  case NS_THEME_CHECKBOX_CONTAINER:
  case NS_THEME_RADIO_CONTAINER:
  case NS_THEME_CHECKBOX_LABEL:
  case NS_THEME_RADIO_LABEL:
#ifdef MOZ_WIDGET_GTK2
  case NS_THEME_MENUBAR:
  case NS_THEME_MENUPOPUP:
  case NS_THEME_MENUITEM:
  case NS_THEME_CHECKMENUITEM:
  case NS_THEME_RADIOMENUITEM:
  case NS_THEME_WINDOW:
  case NS_THEME_DIALOG:
  case NS_THEME_DROPDOWN:
  case NS_THEME_DROPDOWN_TEXT:
#endif
    return !IsWidgetStyled(aPresContext, aFrame, aWidgetType);
  }

  return PR_FALSE;
}

NS_IMETHODIMP_(PRBool)
nsNativeThemeGTK::WidgetIsContainer(PRUint8 aWidgetType)
{
  // XXXdwh At some point flesh all of this out.
  if (aWidgetType == NS_THEME_DROPDOWN_BUTTON || 
      aWidgetType == NS_THEME_RADIO ||
      aWidgetType == NS_THEME_CHECKBOX)
    return PR_FALSE;
  return PR_TRUE;
}
