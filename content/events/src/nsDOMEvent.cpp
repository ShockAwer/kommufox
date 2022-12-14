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
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Steve Clark (buster@netscape.com)
 *   Ilya Konstantinov (mozilla-code@future.shiny.co.il)
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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

#include "nsCOMPtr.h"
#include "nsDOMEvent.h"
#include "nsEventStateManager.h"
#include "nsIFrame.h"
#include "nsIContent.h"
#include "nsIPresShell.h"
#include "nsIDocument.h"
#include "nsIPrivateCompositionEvent.h"
#include "nsIDOMEventTarget.h"
#include "nsIInterfaceRequestor.h"
#include "nsIInterfaceRequestorUtils.h"
#include "prmem.h"
#include "nsLayoutAtoms.h"
#include "nsMutationEvent.h"
#include "nsContentUtils.h"
#include "nsIURI.h"
#include "nsIScriptSecurityManager.h"

static const char* const sEventNames[] = {
  "mousedown", "mouseup", "click", "dblclick", "mouseover",
  "mouseout", "mousemove", "contextmenu", "keydown", "keyup", "keypress",
  "focus", "blur", "load", "beforeunload", "unload", "abort", "error",
  "submit", "reset", "change", "select", "input", "paint" ,"text",
  "compositionstart", "compositionend", "popupshowing", "popupshown",
  "popuphiding", "popuphidden", "close", "command", "broadcast", "commandupdate",
  "dragenter", "dragover", "dragexit", "dragdrop", "draggesture", "resize",
  "scroll","overflow", "underflow", "overflowchanged",
  "DOMSubtreeModified", "DOMNodeInserted", "DOMNodeRemoved", 
  "DOMNodeRemovedFromDocument", "DOMNodeInsertedIntoDocument",
  "DOMAttrModified", "DOMCharacterDataModified",
  "DOMActivate", "DOMFocusIn", "DOMFocusOut",
  "pageshow", "pagehide"
#ifdef MOZ_SVG
 ,
  "SVGLoad", "SVGUnload", "SVGAbort", "SVGError", "SVGResize", "SVGScroll",
  "SVGZoom"
#endif // MOZ_SVG
};

static char *sPopupAllowedEvents;


nsDOMEvent::nsDOMEvent(nsPresContext* aPresContext, nsEvent* aEvent)
{
  mPresContext = aPresContext;

  if (aEvent) {
    mEvent = aEvent;
    mEventIsInternal = PR_FALSE;
  }
  else {
    mEventIsInternal = PR_TRUE;
    /*
      A derived class might want to allocate its own type of aEvent
      (derived from nsEvent). To do this, it should take care to pass
      a non-NULL aEvent to this ctor, e.g.:
      
        nsDOMFooEvent::nsDOMFooEvent(..., nsEvent* aEvent)
        : nsDOMEvent(..., aEvent ? aEvent : new nsFooEvent())
      
      Then, to override the mEventIsInternal assignments done by the
      base ctor, it should do this in its own ctor:

        nsDOMFooEvent::nsDOMFooEvent(..., nsEvent* aEvent)
        ...
        {
          ...
          if (aEvent) {
            mEventIsInternal = PR_FALSE;
          }
          else {
            mEventIsInternal = PR_TRUE;
          }
          ...
        }
     */
    mEvent = new nsEvent(PR_FALSE, 0);
    mEvent->time = PR_Now();
  }

  // Get the explicit original target (if it's anonymous make it null)
  {
    mExplicitOriginalTarget = GetTargetFromFrame();
    mTmpRealOriginalTarget = mExplicitOriginalTarget;
    nsCOMPtr<nsIContent> content = do_QueryInterface(mExplicitOriginalTarget);
    if (content) {
      if (content->IsNativeAnonymous()) {
        mExplicitOriginalTarget = nsnull;
      }
      if (content->GetBindingParent()) {
        mExplicitOriginalTarget = nsnull;
      }
    }
  }
}

nsDOMEvent::~nsDOMEvent() 
{
  NS_ASSERT_OWNINGTHREAD(nsDOMEvent);

  if (mEventIsInternal) {
    delete mEvent->userType;
    delete mEvent;
  }
}

NS_IMPL_ADDREF(nsDOMEvent)
NS_IMPL_RELEASE(nsDOMEvent)

NS_INTERFACE_MAP_BEGIN(nsDOMEvent)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIDOMEvent)
  NS_INTERFACE_MAP_ENTRY(nsIDOMEvent)
  NS_INTERFACE_MAP_ENTRY(nsIDOMNSEvent)
  NS_INTERFACE_MAP_ENTRY(nsIPrivateDOMEvent)
  NS_INTERFACE_MAP_ENTRY_CONTENT_CLASSINFO(Event)
NS_INTERFACE_MAP_END

// nsIDOMEventInterface
NS_METHOD nsDOMEvent::GetType(nsAString& aType)
{
  const char* name = GetEventName(mEvent->message);

  if (name) {
    CopyASCIItoUTF16(name, aType);
    return NS_OK;
  }
  else {
    if (mEvent->message == NS_USER_DEFINED_EVENT && mEvent->userType) {
      aType.Assign(NS_STATIC_CAST(nsStringKey*, mEvent->userType)->GetString());
      return NS_OK;
    }
  }
  
  return NS_ERROR_FAILURE;
}

NS_METHOD nsDOMEvent::GetTarget(nsIDOMEventTarget** aTarget)
{
  if (nsnull != mTarget) {
    *aTarget = mTarget;
    NS_ADDREF(*aTarget);
    return NS_OK;
  }
  
  *aTarget = nsnull;

  nsCOMPtr<nsIContent> targetContent;  

  if (mPresContext) {
    mPresContext->EventStateManager()->
      GetEventTargetContent(mEvent, getter_AddRefs(targetContent));
  }
  
  if (targetContent) {
    mTarget = do_QueryInterface(targetContent);
    if (mTarget) {
      *aTarget = mTarget;
      NS_ADDREF(*aTarget);
    }
  }
  else {
    //Always want a target.  Use document if nothing else.
    nsIPresShell *presShell;
    if (mPresContext && (presShell = mPresContext->GetPresShell())) {
      nsIDocument *doc = presShell->GetDocument();
      if (doc) {
        mTarget = do_QueryInterface(doc);
        if (mTarget) {
          *aTarget = mTarget;
          NS_ADDREF(*aTarget);
        }      
      }
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
nsDOMEvent::GetCurrentTarget(nsIDOMEventTarget** aCurrentTarget)
{
  *aCurrentTarget = mCurrentTarget;
  NS_IF_ADDREF(*aCurrentTarget);
  return NS_OK;
}

//
// Get the actual event target node (may have been retargeted for mouse events)
//
already_AddRefed<nsIDOMEventTarget>
nsDOMEvent::GetTargetFromFrame()
{
  if (!mPresContext) { return nsnull; }

  // Get the target frame (have to get the ESM first)
  nsIFrame* targetFrame = nsnull;
  mPresContext->EventStateManager()->GetEventTarget(&targetFrame);
  if (!targetFrame) { return nsnull; }

  // get the real content
  nsCOMPtr<nsIContent> realEventContent;
  targetFrame->GetContentForEvent(mPresContext, mEvent, getter_AddRefs(realEventContent));
  if (!realEventContent) { return nsnull; }

  // Finally, we have the real content.  QI it and return.
  nsIDOMEventTarget* target = nsnull;
  CallQueryInterface(realEventContent, &target);
  return target;
}

NS_IMETHODIMP
nsDOMEvent::GetExplicitOriginalTarget(nsIDOMEventTarget** aRealEventTarget)
{
  if (mExplicitOriginalTarget) {
    *aRealEventTarget = mExplicitOriginalTarget;
    NS_ADDREF(*aRealEventTarget);
    return NS_OK;
  }

  return GetTarget(aRealEventTarget);
}

NS_IMETHODIMP
nsDOMEvent::GetTmpRealOriginalTarget(nsIDOMEventTarget** aRealEventTarget)
{
  if (mTmpRealOriginalTarget) {
    *aRealEventTarget = mTmpRealOriginalTarget;
    NS_ADDREF(*aRealEventTarget);
    return NS_OK;
  }

  return GetOriginalTarget(aRealEventTarget);
}

NS_IMETHODIMP
nsDOMEvent::GetOriginalTarget(nsIDOMEventTarget** aOriginalTarget)
{
  if (!mOriginalTarget)
    return GetTarget(aOriginalTarget);

  *aOriginalTarget = mOriginalTarget;
  NS_IF_ADDREF(*aOriginalTarget);
  return NS_OK;
}

NS_IMETHODIMP
nsDOMEvent::HasOriginalTarget(PRBool* aResult)
{
  *aResult = (mOriginalTarget != nsnull);
  return NS_OK;
}

NS_IMETHODIMP
nsDOMEvent::SetTrusted(PRBool aTrusted)
{
  if (aTrusted) {
    mEvent->internalAppFlags |= NS_APP_EVENT_FLAG_TRUSTED;
  } else {
    mEvent->internalAppFlags &= ~NS_APP_EVENT_FLAG_TRUSTED;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsDOMEvent::GetEventPhase(PRUint16* aEventPhase)
{
  if (mEvent->flags & NS_EVENT_FLAG_CAPTURE) {
    if (mEvent->flags & NS_EVENT_FLAG_BUBBLE) {
      *aEventPhase = nsIDOMEvent::AT_TARGET;
    }
    else {
      *aEventPhase = nsIDOMEvent::CAPTURING_PHASE;
    }
  }
  else if (mEvent->flags & NS_EVENT_FLAG_BUBBLE) {
    *aEventPhase = nsIDOMEvent::BUBBLING_PHASE;
  }
  else {
    *aEventPhase = 0;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsDOMEvent::GetBubbles(PRBool* aBubbles)
{
  *aBubbles = !(mEvent->flags & NS_EVENT_FLAG_CANT_BUBBLE);
  return NS_OK;
}

NS_IMETHODIMP
nsDOMEvent::GetCancelable(PRBool* aCancelable)
{
  *aCancelable = !(mEvent->flags & NS_EVENT_FLAG_CANT_CANCEL);
  return NS_OK;
}

NS_IMETHODIMP
nsDOMEvent::GetTimeStamp(PRUint64* aTimeStamp)
{
  LL_UI2L(*aTimeStamp, mEvent->time);
  return NS_OK;
}

NS_IMETHODIMP
nsDOMEvent::StopPropagation()
{
  mEvent->flags |= NS_EVENT_FLAG_STOP_DISPATCH;
  return NS_OK;
}

NS_IMETHODIMP
nsDOMEvent::PreventBubble()
{
  if (mEvent->flags & NS_EVENT_FLAG_BUBBLE || mEvent->flags & NS_EVENT_FLAG_INIT) {
    mEvent->flags |= NS_EVENT_FLAG_STOP_DISPATCH;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsDOMEvent::PreventCapture()
{
  if (mEvent->flags & NS_EVENT_FLAG_CAPTURE) {
    mEvent->flags |= NS_EVENT_FLAG_STOP_DISPATCH;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsDOMEvent::GetIsTrusted(PRBool *aIsTrusted)
{
  *aIsTrusted = NS_IS_TRUSTED_EVENT(mEvent);

  return NS_OK;
}

NS_IMETHODIMP
nsDOMEvent::PreventDefault()
{
  if (!(mEvent->flags & NS_EVENT_FLAG_CANT_CANCEL)) {
    mEvent->flags |= NS_EVENT_FLAG_NO_DEFAULT;
  }
  return NS_OK;
}

nsresult
nsDOMEvent::SetEventType(const nsAString& aEventTypeArg)
{
  nsCOMPtr<nsIAtom> atom = do_GetAtom(NS_LITERAL_STRING("on") + aEventTypeArg);
  mEvent->message = NS_USER_DEFINED_EVENT;

  if (mEvent->eventStructType == NS_MOUSE_EVENT) {
    if (atom == nsLayoutAtoms::onmousedown)
      mEvent->message = NS_MOUSE_LEFT_BUTTON_DOWN;
    else if (atom == nsLayoutAtoms::onmouseup)
      mEvent->message = NS_MOUSE_LEFT_BUTTON_UP;
    else if (atom == nsLayoutAtoms::onclick)
      mEvent->message = NS_MOUSE_LEFT_CLICK;
    else if (atom == nsLayoutAtoms::ondblclick)
      mEvent->message = NS_MOUSE_LEFT_DOUBLECLICK;
    else if (atom == nsLayoutAtoms::onmouseover)
      mEvent->message = NS_MOUSE_ENTER_SYNTH;
    else if (atom == nsLayoutAtoms::onmouseout)
      mEvent->message = NS_MOUSE_EXIT_SYNTH;
    else if (atom == nsLayoutAtoms::onmousemove)
      mEvent->message = NS_MOUSE_MOVE;
    else if (atom == nsLayoutAtoms::oncontextmenu)
      mEvent->message = NS_CONTEXTMENU;
  } else if (mEvent->eventStructType == NS_KEY_EVENT) {
    if (atom == nsLayoutAtoms::onkeydown)
      mEvent->message = NS_KEY_DOWN;
    else if (atom == nsLayoutAtoms::onkeyup)
      mEvent->message = NS_KEY_UP;
    else if (atom == nsLayoutAtoms::onkeypress)
      mEvent->message = NS_KEY_PRESS;
  } else if (mEvent->eventStructType == NS_COMPOSITION_EVENT) {
    if (atom == nsLayoutAtoms::oncompositionstart)
      mEvent->message = NS_COMPOSITION_START;
    else if (atom == nsLayoutAtoms::oncompositionend)
      mEvent->message = NS_COMPOSITION_END;
  } else if (mEvent->eventStructType == NS_EVENT) {
    if (atom == nsLayoutAtoms::onfocus)
      mEvent->message = NS_FOCUS_CONTENT;
    else if (atom == nsLayoutAtoms::onblur)
      mEvent->message = NS_BLUR_CONTENT;
    else if (atom == nsLayoutAtoms::onsubmit)
      mEvent->message = NS_FORM_SUBMIT;
    else if (atom == nsLayoutAtoms::onreset)
      mEvent->message = NS_FORM_RESET;
    else if (atom == nsLayoutAtoms::onchange)
      mEvent->message = NS_FORM_CHANGE;
    else if (atom == nsLayoutAtoms::onselect)
      mEvent->message = NS_FORM_SELECTED;
    else if (atom == nsLayoutAtoms::onload)
      mEvent->message = NS_PAGE_LOAD;
    else if (atom == nsLayoutAtoms::onunload)
      mEvent->message = NS_PAGE_UNLOAD;
    else if (atom == nsLayoutAtoms::onabort)
      mEvent->message = NS_IMAGE_ABORT;
    else if (atom == nsLayoutAtoms::onerror)
      mEvent->message = NS_IMAGE_ERROR;
  } else if (mEvent->eventStructType == NS_MUTATION_EVENT) {
    if (atom == nsLayoutAtoms::onDOMAttrModified)
      mEvent->message = NS_MUTATION_ATTRMODIFIED;
    else if (atom == nsLayoutAtoms::onDOMCharacterDataModified)
      mEvent->message = NS_MUTATION_CHARACTERDATAMODIFIED;
    else if (atom == nsLayoutAtoms::onDOMNodeInserted)
      mEvent->message = NS_MUTATION_NODEINSERTED;
    else if (atom == nsLayoutAtoms::onDOMNodeRemoved)
      mEvent->message = NS_MUTATION_NODEREMOVED;
    else if (atom == nsLayoutAtoms::onDOMNodeInsertedIntoDocument)
      mEvent->message = NS_MUTATION_NODEINSERTEDINTODOCUMENT;
    else if (atom == nsLayoutAtoms::onDOMNodeRemovedFromDocument)
      mEvent->message = NS_MUTATION_NODEREMOVEDFROMDOCUMENT;
    else if (atom == nsLayoutAtoms::onDOMSubtreeModified)
      mEvent->message = NS_MUTATION_SUBTREEMODIFIED;
  } else if (mEvent->eventStructType == NS_UI_EVENT) {
    if (atom == nsLayoutAtoms::onDOMActivate)
      mEvent->message = NS_UI_ACTIVATE;
    else if (atom == nsLayoutAtoms::onDOMFocusIn)
      mEvent->message = NS_UI_FOCUSIN;
    else if (atom == nsLayoutAtoms::onDOMFocusOut)
      mEvent->message = NS_UI_FOCUSOUT;
    else if (atom == nsLayoutAtoms::oninput)
      mEvent->message = NS_FORM_INPUT;
  } else if (mEvent->eventStructType == NS_PAGETRANSITION_EVENT) {
    if (atom == nsLayoutAtoms::onpageshow)
      mEvent->message = NS_PAGE_SHOW;
    else if (atom == nsLayoutAtoms::onpagehide)
      mEvent->message = NS_PAGE_HIDE;
  }
#ifdef MOZ_SVG
  else if (mEvent->eventStructType == NS_SVG_EVENT) {
    if (atom == nsLayoutAtoms::onSVGLoad)
      mEvent->message = NS_SVG_LOAD;
    else if (atom == nsLayoutAtoms::onSVGUnload)
      mEvent->message = NS_SVG_UNLOAD;
    else if (atom == nsLayoutAtoms::onSVGAbort)
      mEvent->message = NS_SVG_ABORT;
    else if (atom == nsLayoutAtoms::onSVGError)
      mEvent->message = NS_SVG_ERROR;
    else if (atom == nsLayoutAtoms::onSVGResize)
      mEvent->message = NS_SVG_RESIZE;
    else if (atom == nsLayoutAtoms::onSVGScroll)
      mEvent->message = NS_SVG_SCROLL;
  } else if (mEvent->eventStructType == NS_SVGZOOM_EVENT) {
    if (atom == nsLayoutAtoms::onSVGZoom)
      mEvent->message = NS_SVG_ZOOM;
  }
#endif // MOZ_SVG

  if (mEvent->message == NS_USER_DEFINED_EVENT)
    mEvent->userType = new nsStringKey(aEventTypeArg);

  return NS_OK;
}

NS_IMETHODIMP
nsDOMEvent::InitEvent(const nsAString& aEventTypeArg, PRBool aCanBubbleArg, PRBool aCancelableArg)
{
  // Make sure this event isn't already being dispatched.
  NS_ENSURE_TRUE(!NS_IS_EVENT_IN_DISPATCH(mEvent), NS_ERROR_INVALID_ARG);

  if (NS_IS_TRUSTED_EVENT(mEvent)) {
    // Ensure the caller is permitted to dispatch trusted DOM events.

    PRBool enabled = PR_FALSE;
    nsContentUtils::GetSecurityManager()->
      IsCapabilityEnabled("UniversalBrowserWrite", &enabled);

    if (!enabled) {
      SetTrusted(PR_FALSE);
    }
  }

  NS_ENSURE_SUCCESS(SetEventType(aEventTypeArg), NS_ERROR_FAILURE);

  mEvent->flags |=
    aCanBubbleArg ? NS_EVENT_FLAG_NONE : NS_EVENT_FLAG_CANT_BUBBLE;
  mEvent->flags |=
    aCancelableArg ? NS_EVENT_FLAG_NONE : NS_EVENT_FLAG_CANT_CANCEL;

  // Unset the NS_EVENT_FLAG_STOP_DISPATCH_IMMEDIATELY bit (which is
  // set at the end of event dispatch) so that this event can be
  // dispatched.
  mEvent->flags &= ~NS_EVENT_FLAG_STOP_DISPATCH_IMMEDIATELY;

  return NS_OK;
}

NS_METHOD nsDOMEvent::DuplicatePrivateData()
{
  //XXX Write me!

  //XXX And when you do, make sure to copy over the event target here, too!
  return NS_OK;
}

NS_METHOD nsDOMEvent::SetTarget(nsIDOMEventTarget* aTarget)
{
#ifdef DEBUG
  {
    nsCOMPtr<nsPIDOMWindow> win = do_QueryInterface(aTarget);

    NS_ASSERTION(!win || !win->IsInnerWindow(),
                 "Uh, inner window set as event target!");
  }
#endif

  mTarget = aTarget;
  return NS_OK;
}

NS_METHOD nsDOMEvent::SetCurrentTarget(nsIDOMEventTarget* aCurrentTarget)
{
#ifdef DEBUG
  {
    nsCOMPtr<nsPIDOMWindow> win = do_QueryInterface(aCurrentTarget);

    NS_ASSERTION(!win || !win->IsInnerWindow(),
                 "Uh, inner window set as event target!");
  }
#endif

  mCurrentTarget = aCurrentTarget;
  return NS_OK;
}

NS_METHOD nsDOMEvent::SetOriginalTarget(nsIDOMEventTarget* aOriginalTarget)
{
#ifdef DEBUG
  {
    nsCOMPtr<nsPIDOMWindow> win = do_QueryInterface(aOriginalTarget);

    NS_ASSERTION(!win || !win->IsInnerWindow(),
                 "Uh, inner window set as event target!");
  }
#endif

  mOriginalTarget = aOriginalTarget;
  return NS_OK;
}

NS_IMETHODIMP
nsDOMEvent::IsDispatchStopped(PRBool* aIsDispatchStopped)
{
  if (mEvent->flags & NS_EVENT_FLAG_STOP_DISPATCH) {
    *aIsDispatchStopped = PR_TRUE;
  } else {
    *aIsDispatchStopped = PR_FALSE;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsDOMEvent::IsHandled(PRBool* aIsHandled)
{
  if (mEvent->internalAppFlags | NS_APP_EVENT_FLAG_HANDLED) {
    *aIsHandled = PR_TRUE;
  } else {
    *aIsHandled = PR_FALSE;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsDOMEvent::SetHandled(PRBool aHandled)
{
  if(aHandled)
    mEvent->internalAppFlags |= NS_APP_EVENT_FLAG_HANDLED;
  else
    mEvent->internalAppFlags &= ~NS_APP_EVENT_FLAG_HANDLED;

  return NS_OK;
}

NS_IMETHODIMP
nsDOMEvent::GetInternalNSEvent(nsEvent** aNSEvent)
{
  NS_ENSURE_ARG_POINTER(aNSEvent);
  *aNSEvent = mEvent;
  return NS_OK;
}

// return true if eventName is contained within events, delimited by
// spaces
static PRBool
PopupAllowedForEvent(const char *eventName)
{
  if (!sPopupAllowedEvents) {
    nsDOMEvent::PopupAllowedEventsChanged();

    if (!sPopupAllowedEvents) {
      return PR_FALSE;
    }
  }

  nsDependentCString events(sPopupAllowedEvents);

  nsAFlatCString::const_iterator start, end;
  nsAFlatCString::const_iterator startiter(events.BeginReading(start));
  events.EndReading(end);

  while (startiter != end) {
    nsAFlatCString::const_iterator enditer(end);

    if (!FindInReadable(nsDependentCString(eventName), startiter, enditer))
      return PR_FALSE;

    // the match is surrounded by spaces, or at a string boundary
    if ((startiter == start || *--startiter == ' ') &&
        (enditer == end || *enditer == ' ')) {
      return PR_TRUE;
    }

    // Move on and see if there are other matches. (The delimitation
    // requirement makes it pointless to begin the next search before
    // the end of the invalid match just found.)
    startiter = enditer;
  }

  return PR_FALSE;
}

// static
PopupControlState
nsDOMEvent::GetEventPopupControlState(nsEvent *aEvent)
{
  // generally if an event handler is running, new windows are disallowed.
  // check for exceptions:
  PopupControlState abuse = openAbused;

  switch(aEvent->eventStructType) {
  case NS_EVENT :
    // For these following events only allow popups if they're
    // triggered while handling user input. See
    // nsPresShell::HandleEventInternal() for details.
    if (nsEventStateManager::IsHandlingUserInput()) {
      switch(aEvent->message) {
      case NS_FORM_SELECTED :
        if (::PopupAllowedForEvent("select"))
          abuse = openControlled;
        break;
      case NS_FORM_CHANGE :
        if (::PopupAllowedForEvent("change"))
          abuse = openControlled;
        break;
      }
    }
    break;
  case NS_GUI_EVENT :
    // For this following event only allow popups if it's triggered
    // while handling user input. See
    // nsPresShell::HandleEventInternal() for details.
    if (nsEventStateManager::IsHandlingUserInput()) {
      switch(aEvent->message) {
      case NS_FORM_INPUT :
        if (::PopupAllowedForEvent("input"))
          abuse = openControlled;
        break;
      }
    }
    break;
  case NS_INPUT_EVENT :
    // For this following event only allow popups if it's triggered
    // while handling user input. See
    // nsPresShell::HandleEventInternal() for details.
    if (nsEventStateManager::IsHandlingUserInput()) {
      switch(aEvent->message) {
      case NS_FORM_CHANGE :
        if (::PopupAllowedForEvent("change"))
          abuse = openControlled;
        break;
      }
    }
    break;
  case NS_KEY_EVENT :
    if (NS_IS_TRUSTED_EVENT(aEvent)) {
      PRUint32 key = NS_STATIC_CAST(nsKeyEvent *, aEvent)->keyCode;
      switch(aEvent->message) {
      case NS_KEY_PRESS :
        // return key on focused button. see note at NS_MOUSE_LEFT_CLICK.
        if (key == nsIDOMKeyEvent::DOM_VK_RETURN)
          abuse = openAllowed;
        else if (::PopupAllowedForEvent("keypress"))
          abuse = openControlled;
        break;
      case NS_KEY_UP :
        // space key on focused button. see note at NS_MOUSE_LEFT_CLICK.
        if (key == nsIDOMKeyEvent::DOM_VK_SPACE)
          abuse = openAllowed;
        else if (::PopupAllowedForEvent("keyup"))
          abuse = openControlled;
        break;
      case NS_KEY_DOWN :
        if (::PopupAllowedForEvent("keydown"))
          abuse = openControlled;
        break;
      }
    }
    break;
  case NS_MOUSE_EVENT :
    if (NS_IS_TRUSTED_EVENT(aEvent)) {
      switch(aEvent->message) {
      case NS_MOUSE_LEFT_BUTTON_UP :
        if (::PopupAllowedForEvent("mouseup"))
          abuse = openControlled;
        break;
      case NS_MOUSE_LEFT_BUTTON_DOWN :
        if (::PopupAllowedForEvent("mousedown"))
          abuse = openControlled;
        break;
      case NS_MOUSE_LEFT_CLICK :
        /* Click events get special treatment because of their
           historical status as a more legitimate event handler. If
           click popups are enabled in the prefs, clear the popup
           status completely. */
        if (::PopupAllowedForEvent("click"))
          abuse = openAllowed;
        break;
      case NS_MOUSE_LEFT_DOUBLECLICK :
        if (::PopupAllowedForEvent("dblclick"))
          abuse = openControlled;
        break;
      }
    }
    break;
  case NS_SCRIPT_ERROR_EVENT :
    switch(aEvent->message) {
    case NS_SCRIPT_ERROR :
      // Any error event will allow popups, if enabled in the pref.
      if (::PopupAllowedForEvent("error"))
        abuse = openControlled;
      break;
    }
    break;
  case NS_FORM_EVENT :
    // For these following events only allow popups if they're
    // triggered while handling user input. See
    // nsPresShell::HandleEventInternal() for details.
    if (nsEventStateManager::IsHandlingUserInput()) {
      switch(aEvent->message) {
      case NS_FORM_SUBMIT :
        if (::PopupAllowedForEvent("submit"))
          abuse = openControlled;
        break;
      case NS_FORM_RESET :
        if (::PopupAllowedForEvent("reset"))
          abuse = openControlled;
        break;
      }
    }
    break;
  }

  return abuse;
}

// static
void
nsDOMEvent::PopupAllowedEventsChanged()
{
  if (sPopupAllowedEvents) {
    nsMemory::Free(sPopupAllowedEvents);
  }

  nsAdoptingCString str =
    nsContentUtils::GetCharPref("dom.popup_allowed_events");

  // We'll want to do this even if str is empty to avoid looking up
  // this pref all the time if it's not set.
  sPopupAllowedEvents = ToNewCString(str);
}

// static
void
nsDOMEvent::Shutdown()
{
  if (sPopupAllowedEvents) {
    nsMemory::Free(sPopupAllowedEvents);
  }
}

// static
const char* nsDOMEvent::GetEventName(PRUint32 aEventType)
{
  switch(aEventType) {
  case NS_MOUSE_LEFT_BUTTON_DOWN:
  case NS_MOUSE_MIDDLE_BUTTON_DOWN:
  case NS_MOUSE_RIGHT_BUTTON_DOWN:
    return sEventNames[eDOMEvents_mousedown];
  case NS_MOUSE_LEFT_BUTTON_UP:
  case NS_MOUSE_MIDDLE_BUTTON_UP:
  case NS_MOUSE_RIGHT_BUTTON_UP:
    return sEventNames[eDOMEvents_mouseup];
  case NS_MOUSE_LEFT_CLICK:
  case NS_MOUSE_MIDDLE_CLICK:
  case NS_MOUSE_RIGHT_CLICK:
    return sEventNames[eDOMEvents_click];
  case NS_MOUSE_LEFT_DOUBLECLICK:
  case NS_MOUSE_MIDDLE_DOUBLECLICK:
  case NS_MOUSE_RIGHT_DOUBLECLICK:
    return sEventNames[eDOMEvents_dblclick];
  case NS_MOUSE_ENTER_SYNTH:
    return sEventNames[eDOMEvents_mouseover];
  case NS_MOUSE_EXIT_SYNTH:
    return sEventNames[eDOMEvents_mouseout];
  case NS_MOUSE_MOVE:
    return sEventNames[eDOMEvents_mousemove];
  case NS_KEY_UP:
    return sEventNames[eDOMEvents_keyup];
  case NS_KEY_DOWN:
    return sEventNames[eDOMEvents_keydown];
  case NS_KEY_PRESS:
    return sEventNames[eDOMEvents_keypress];
  case NS_COMPOSITION_START:
    return sEventNames[eDOMEvents_compositionstart];
  case NS_COMPOSITION_END:
    return sEventNames[eDOMEvents_compositionend];
  case NS_FOCUS_CONTENT:
    return sEventNames[eDOMEvents_focus];
  case NS_BLUR_CONTENT:
    return sEventNames[eDOMEvents_blur];
  case NS_XUL_CLOSE:
    return sEventNames[eDOMEvents_close];
  case NS_PAGE_LOAD:
  case NS_IMAGE_LOAD:
  case NS_SCRIPT_LOAD:
    return sEventNames[eDOMEvents_load];
  case NS_BEFORE_PAGE_UNLOAD:
    return sEventNames[eDOMEvents_beforeunload];
  case NS_PAGE_UNLOAD:
    return sEventNames[eDOMEvents_unload];
  case NS_IMAGE_ABORT:
    return sEventNames[eDOMEvents_abort];
  case NS_IMAGE_ERROR:
  case NS_SCRIPT_ERROR:
    return sEventNames[eDOMEvents_error];
  case NS_FORM_SUBMIT:
    return sEventNames[eDOMEvents_submit];
  case NS_FORM_RESET:
    return sEventNames[eDOMEvents_reset];
  case NS_FORM_CHANGE:
    return sEventNames[eDOMEvents_change];
  case NS_FORM_SELECTED:
    return sEventNames[eDOMEvents_select];
  case NS_FORM_INPUT:
    return sEventNames[eDOMEvents_input];
  case NS_PAINT:
    return sEventNames[eDOMEvents_paint];
  case NS_RESIZE_EVENT:
    return sEventNames[eDOMEvents_resize];
  case NS_SCROLL_EVENT:
    return sEventNames[eDOMEvents_scroll];
  case NS_TEXT_TEXT:
    return sEventNames[eDOMEvents_text];
  case NS_XUL_POPUP_SHOWING:
    return sEventNames[eDOMEvents_popupShowing];
  case NS_XUL_POPUP_SHOWN:
    return sEventNames[eDOMEvents_popupShown];
  case NS_XUL_POPUP_HIDING:
    return sEventNames[eDOMEvents_popupHiding];
  case NS_XUL_POPUP_HIDDEN:
    return sEventNames[eDOMEvents_popupHidden];
  case NS_XUL_COMMAND:
    return sEventNames[eDOMEvents_command];
  case NS_XUL_BROADCAST:
    return sEventNames[eDOMEvents_broadcast];
  case NS_XUL_COMMAND_UPDATE:
    return sEventNames[eDOMEvents_commandupdate];
  case NS_DRAGDROP_ENTER:
    return sEventNames[eDOMEvents_dragenter];
  case NS_DRAGDROP_OVER_SYNTH:
    return sEventNames[eDOMEvents_dragover];
  case NS_DRAGDROP_EXIT_SYNTH:
    return sEventNames[eDOMEvents_dragexit];
  case NS_DRAGDROP_DROP:
    return sEventNames[eDOMEvents_dragdrop];
  case NS_DRAGDROP_GESTURE:
    return sEventNames[eDOMEvents_draggesture];
  case NS_SCROLLPORT_OVERFLOW:
    return sEventNames[eDOMEvents_overflow];
  case NS_SCROLLPORT_UNDERFLOW:
    return sEventNames[eDOMEvents_underflow];
  case NS_SCROLLPORT_OVERFLOWCHANGED:
    return sEventNames[eDOMEvents_overflowchanged];
  case NS_MUTATION_SUBTREEMODIFIED:
    return sEventNames[eDOMEvents_subtreemodified];
  case NS_MUTATION_NODEINSERTED:
    return sEventNames[eDOMEvents_nodeinserted];
  case NS_MUTATION_NODEREMOVED:
    return sEventNames[eDOMEvents_noderemoved];
  case NS_MUTATION_NODEREMOVEDFROMDOCUMENT:
    return sEventNames[eDOMEvents_noderemovedfromdocument];
  case NS_MUTATION_NODEINSERTEDINTODOCUMENT:
    return sEventNames[eDOMEvents_nodeinsertedintodocument];
  case NS_MUTATION_ATTRMODIFIED:
    return sEventNames[eDOMEvents_attrmodified];
  case NS_MUTATION_CHARACTERDATAMODIFIED:
    return sEventNames[eDOMEvents_characterdatamodified];
  case NS_CONTEXTMENU:
  case NS_CONTEXTMENU_KEY:
    return sEventNames[eDOMEvents_contextmenu];
  case NS_UI_ACTIVATE:
    return sEventNames[eDOMEvents_DOMActivate];
  case NS_UI_FOCUSIN:
    return sEventNames[eDOMEvents_DOMFocusIn];
  case NS_UI_FOCUSOUT:
    return sEventNames[eDOMEvents_DOMFocusOut];
  case NS_PAGE_SHOW:
    return sEventNames[eDOMEvents_pageshow];
  case NS_PAGE_HIDE:
    return sEventNames[eDOMEvents_pagehide];
#ifdef MOZ_SVG
  case NS_SVG_LOAD:
    return sEventNames[eDOMEvents_SVGLoad];
  case NS_SVG_UNLOAD:
    return sEventNames[eDOMEvents_SVGUnload];
  case NS_SVG_ABORT:
    return sEventNames[eDOMEvents_SVGAbort];
  case NS_SVG_ERROR:
    return sEventNames[eDOMEvents_SVGError];
  case NS_SVG_RESIZE:
    return sEventNames[eDOMEvents_SVGResize];
  case NS_SVG_SCROLL:
    return sEventNames[eDOMEvents_SVGScroll];
  case NS_SVG_ZOOM:
    return sEventNames[eDOMEvents_SVGZoom];
#endif // MOZ_SVG
  default:
    break;
  }
  // XXXldb We can hit this case in many ways, partly thanks to
  // nsDOMEvent::SetEventType being incomplete, and partly due to some
  // event types not having message constants at all (e.g., popup
  // blocked events).  Shouldn't we use the event's userType when we
  // can?
  return nsnull;
}

nsresult NS_NewDOMEvent(nsIDOMEvent** aInstancePtrResult,
                        nsPresContext* aPresContext,
                        nsEvent *aEvent) 
{
  nsDOMEvent* it = new nsDOMEvent(aPresContext, aEvent);
  if (nsnull == it) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  return CallQueryInterface(it, aInstancePtrResult);
}
