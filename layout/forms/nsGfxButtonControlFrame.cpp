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

#include "nsGfxButtonControlFrame.h"
#include "nsIButton.h"
#include "nsWidgetsCID.h"
#include "nsIFontMetrics.h"
#include "nsFormControlFrame.h"
#include "nsIFormControl.h"
#include "nsISupportsArray.h"
#include "nsINameSpaceManager.h"
#ifdef ACCESSIBILITY
#include "nsIAccessibilityService.h"
#endif
#include "nsIServiceManager.h"
#include "nsIDOMNode.h"
#include "nsLayoutAtoms.h"
#include "nsReflowPath.h"
#include "nsAutoPtr.h"
#include "nsStyleSet.h"
#include "nsContentUtils.h"
// MouseEvent suppression in PP
#include "nsGUIEvent.h"

#include "nsNodeInfoManager.h"

const nscoord kSuggestedNotSet = -1;

nsGfxButtonControlFrame::nsGfxButtonControlFrame()
{
  mSuggestedWidth  = kSuggestedNotSet;
  mSuggestedHeight = kSuggestedNotSet;
}

nsresult
NS_NewGfxButtonControlFrame(nsIPresShell* aPresShell, nsIFrame** aNewFrame)
{
  NS_PRECONDITION(aNewFrame, "null OUT ptr");
  if (nsnull == aNewFrame) {
    return NS_ERROR_NULL_POINTER;
  }
  nsGfxButtonControlFrame* it = new (aPresShell) nsGfxButtonControlFrame;
  if (!it) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  *aNewFrame = it;
  return NS_OK;
}
      
nsIAtom*
nsGfxButtonControlFrame::GetType() const
{
  return nsLayoutAtoms::gfxButtonControlFrame;
}

// Special check for the browse button of a file input.
//
// We'll return PR_TRUE if type is NS_FORM_INPUT_BUTTON and our parent
// is a file input.
PRBool
nsGfxButtonControlFrame::IsFileBrowseButton(PRInt32 type)
{
  PRBool rv = PR_FALSE;
  if (NS_FORM_INPUT_BUTTON == type) {
    // Check to see if parent is a file input
    nsCOMPtr<nsIFormControl> formCtrl =
      do_QueryInterface(mContent->GetParent());

    rv = formCtrl && formCtrl->GetType() == NS_FORM_INPUT_FILE;
  }
  return rv;
}

/*
 * FIXME: this ::GetIID() method has no meaning in life and should be
 * removed.
 * Pierre Phaneuf <pp@ludusdesign.com>
 */
const nsIID&
nsGfxButtonControlFrame::GetIID()
{
  return NS_GET_IID(nsIButton);
}
  
const nsIID&
nsGfxButtonControlFrame::GetCID()
{
  static NS_DEFINE_IID(kButtonCID, NS_BUTTON_CID);
  return kButtonCID;
}

#ifdef DEBUG
NS_IMETHODIMP
nsGfxButtonControlFrame::GetFrameName(nsAString& aResult) const
{
  return MakeFrameName(NS_LITERAL_STRING("ButtonControl"), aResult);
}
#endif

NS_IMETHODIMP 
nsGfxButtonControlFrame::AddComputedBorderPaddingToDesiredSize(nsHTMLReflowMetrics& aDesiredSize,
                                                               const nsHTMLReflowState& aSuggestedReflowState)
{
  if (kSuggestedNotSet == mSuggestedWidth) {
    aDesiredSize.width  += aSuggestedReflowState.mComputedBorderPadding.left + aSuggestedReflowState.mComputedBorderPadding.right;
  }

  if (kSuggestedNotSet == mSuggestedHeight) {
    aDesiredSize.height += aSuggestedReflowState.mComputedBorderPadding.top + aSuggestedReflowState.mComputedBorderPadding.bottom;
  }
  return NS_OK;
}

// Create the text content used as label for the button.
// The frame will be generated by the frame constructor.
NS_IMETHODIMP
nsGfxButtonControlFrame::CreateAnonymousContent(nsPresContext* aPresContext,
                                                nsISupportsArray& aChildList)
{
  nsresult result;

  // Get the text from the "value" attribute.
  // If it is zero length, set it to a default value (localized)
  nsAutoString initvalue;
  result = GetValue(&initvalue);
  nsXPIDLString value;
  value.Assign(initvalue);
  if (result != NS_CONTENT_ATTR_HAS_VALUE && value.IsEmpty()) {
    // Generate localized label.
    // We can't make any assumption as to what the default would be
    // because the value is localized for non-english platforms, thus
    // it might not be the string "Reset", "Submit Query", or "Browse..."
    result = GetDefaultLabel(value);
    if (NS_FAILED(result)) {
      return result;
    }
  }

  // Compress whitespace out of label if needed.
  if (!GetStyleText()->WhiteSpaceIsSignificant()) {
    value.CompressWhitespace();
  } else if (value.Length() > 2 && value.First() == ' ' &&
             value.CharAt(value.Length() - 1) == ' ') {
    // This is a bit of a hack.  The reason this is here is as follows: we now
    // have default padding on our buttons to make them non-ugly.
    // Unfortunately, IE-windows does not have such padding, so people will
    // stick values like " ok " (with the spaces) in the buttons in an attempt
    // to make them look decent.  Unfortunately, if they do this the button
    // looks way too big in Mozilla.  Worse yet, if they do this _and_ set a
    // fixed width for the button we run into trouble because our focus-rect
    // border/padding and outer border take up 10px of the horizontal button
    // space or so; the result is that the text is misaligned, even with the
    // recentering we do in nsHTMLButtonFrame::Reflow.  So to solve this, even
    // if the whitespace is significant, single leading and trailing _spaces_
    // (and not other whitespace) are removed.  The proper solution, of
    // course, is to not have the focus rect painting taking up 6px of
    // horizontal space. We should do that instead (via XBL form controls or
    // changing the renderer) and remove this.
    value.Cut(0, 1);
    value.Truncate(value.Length() - 1);
  }

  // Add a child text content node for the label
  nsCOMPtr<nsITextContent> labelContent;
  nsINodeInfo* nodeInfo = mContent->GetNodeInfo();
  if (nodeInfo) {
    NS_NewTextNode(getter_AddRefs(labelContent), nodeInfo->NodeInfoManager());
  }
  if (labelContent) {
    // set the value of the text node and add it to the child list
    mTextContent.swap(labelContent);
    mTextContent->SetText(value, PR_TRUE);
    aChildList.AppendElement(mTextContent);
  }
  return result;
}

// Create the text content used as label for the button.
// The frame will be generated by the frame constructor.
NS_IMETHODIMP
nsGfxButtonControlFrame::CreateFrameFor(nsPresContext*   aPresContext,
                                        nsIContent *      aContent,
                                        nsIFrame**        aFrame)
{
  nsIFrame * newFrame = nsnull;
  nsresult rv = NS_ERROR_FAILURE;

  if (aFrame) 
    *aFrame = nsnull; 

  nsCOMPtr<nsIContent> content(do_QueryInterface(mTextContent));
  if (aContent == content.get()) {
    nsIFrame * parentFrame = mFrames.FirstChild();
    nsStyleContext* styleContext = parentFrame->GetStyleContext();

    rv = NS_NewTextFrame(aPresContext->PresShell(), &newFrame);
    if (NS_FAILED(rv)) { return rv; }
    if (!newFrame)   { return NS_ERROR_NULL_POINTER; }
    nsRefPtr<nsStyleContext> textStyleContext;
    textStyleContext = aPresContext->StyleSet()->
      ResolveStyleForNonElement(styleContext);
    if (!textStyleContext) { return NS_ERROR_NULL_POINTER; }

    if (styleContext) {
      // initialize the text frame
      newFrame->Init(aPresContext, content, parentFrame, textStyleContext, nsnull);
      newFrame->SetInitialChildList(aPresContext, nsnull, nsnull);
      rv = NS_OK;
    }
  }

  if (aFrame) {
    *aFrame = newFrame;
  }
  return rv;
}

#ifdef ACCESSIBILITY
NS_IMETHODIMP nsGfxButtonControlFrame::GetAccessible(nsIAccessible** aAccessible)
{
  nsCOMPtr<nsIAccessibilityService> accService = do_GetService("@mozilla.org/accessibilityService;1");

  if (accService) {
    return accService->CreateHTMLButtonAccessible(NS_STATIC_CAST(nsIFrame*, this), aAccessible);
  }

  return NS_ERROR_FAILURE;
}
#endif

// Frames are not refcounted, no need to AddRef
NS_IMETHODIMP
nsGfxButtonControlFrame::QueryInterface(const nsIID& aIID, void** aInstancePtr)
{
  NS_ENSURE_ARG_POINTER(aInstancePtr);

  if (aIID.Equals(NS_GET_IID(nsIAnonymousContentCreator))) {
    *aInstancePtr = NS_STATIC_CAST(nsIAnonymousContentCreator*, this);
  }
else {
    return nsHTMLButtonControlFrame::QueryInterface(aIID, aInstancePtr);
  }

  return NS_OK;
}

// Initially we hardcoded the default strings here.
// Next, we used html.css to store the default label for various types
// of buttons. (nsGfxButtonControlFrame::DoNavQuirksReflow rev 1.20)
// However, since html.css is not internationalized, we now grab the default
// label from a string bundle as is done for all other UI strings.
// See bug 16999 for further details.
nsresult
nsGfxButtonControlFrame::GetDefaultLabel(nsXPIDLString& aString) 
{
  nsresult rv = NS_OK;
  PRInt32 type = GetFormControlType();
  const char *prop;
  if (type == NS_FORM_INPUT_RESET) {
    prop = "Reset";
  } 
  else if (type == NS_FORM_INPUT_SUBMIT) {
    prop = "Submit";
  } 
  else if (IsFileBrowseButton(type)) {
    prop = "Browse";
  }
  else {
    aString.Truncate();
    return NS_OK;
  }

  return nsContentUtils::GetLocalizedString(nsContentUtils::eFORMS_PROPERTIES,
                                            prop, aString);
}

NS_IMETHODIMP
nsGfxButtonControlFrame::AttributeChanged(nsIContent*     aChild,
                                          PRInt32         aNameSpaceID,
                                          nsIAtom*        aAttribute,
                                          PRInt32         aModType)
{
  nsresult rv = NS_OK;

  // If the value attribute is set, update the text of the label
  if (nsHTMLAtoms::value == aAttribute) {
    nsAutoString value;
    if (mTextContent && mContent) {
      if (NS_CONTENT_ATTR_HAS_VALUE != mContent->GetAttr(kNameSpaceID_None, nsHTMLAtoms::value, value)) {
        value.Truncate();
      }
      mTextContent->SetText(value, PR_TRUE);
    } else {
      rv = NS_ERROR_UNEXPECTED;
    }

  // defer to HTMLButtonControlFrame
  } else {
    rv = nsHTMLButtonControlFrame::AttributeChanged(aChild, aNameSpaceID, aAttribute, aModType);
  }
  return rv;
}

PRBool
nsGfxButtonControlFrame::IsLeaf() const
{
  return PR_TRUE;
}

NS_IMETHODIMP 
nsGfxButtonControlFrame::Reflow(nsPresContext*          aPresContext, 
                                nsHTMLReflowMetrics&     aDesiredSize,
                                const nsHTMLReflowState& aReflowState, 
                                nsReflowStatus&          aStatus)
{
  DO_GLOBAL_REFLOW_COUNT("nsGfxButtonControlFrame", aReflowState.reason);
  DISPLAY_REFLOW(aPresContext, this, aReflowState, aDesiredSize, aStatus);

  if ((kSuggestedNotSet != mSuggestedWidth) || 
      (kSuggestedNotSet != mSuggestedHeight)) {
    nsHTMLReflowState suggestedReflowState(aReflowState);

      // Honor the suggested width and/or height.
    if (kSuggestedNotSet != mSuggestedWidth) {
      suggestedReflowState.mComputedWidth = mSuggestedWidth;
    }

    if (kSuggestedNotSet != mSuggestedHeight) {
      suggestedReflowState.mComputedHeight = mSuggestedHeight;
    }

    return nsHTMLButtonControlFrame::Reflow(aPresContext, aDesiredSize, suggestedReflowState, aStatus);

  }
  
  // Normal reflow.

  return nsHTMLButtonControlFrame::Reflow(aPresContext, aDesiredSize, aReflowState, aStatus);
}

NS_IMETHODIMP 
nsGfxButtonControlFrame::SetSuggestedSize(nscoord aWidth, nscoord aHeight)
{
  mSuggestedWidth = aWidth;
  mSuggestedHeight = aHeight;
  //mState |= NS_FRAME_IS_DIRTY;
  return NS_OK;
}

NS_IMETHODIMP
nsGfxButtonControlFrame::HandleEvent(nsPresContext* aPresContext, 
                                      nsGUIEvent*     aEvent,
                                      nsEventStatus*  aEventStatus)
{
  // temp fix until Bug 124990 gets fixed
  if (aPresContext->IsPaginated() && NS_IS_MOUSE_EVENT(aEvent)) {
    return NS_OK;
  }
  // Override the HandleEvent to prevent the nsFrame::HandleEvent
  // from being called. The nsFrame::HandleEvent causes the button label
  // to be selected (Drawn with an XOR rectangle over the label)

  // do we have user-input style?
  const nsStyleUserInterface* uiStyle = GetStyleUserInterface();
  if (uiStyle->mUserInput == NS_STYLE_USER_INPUT_NONE || uiStyle->mUserInput == NS_STYLE_USER_INPUT_DISABLED)
    return nsFrame::HandleEvent(aPresContext, aEvent, aEventStatus);
  
  return NS_OK;
}