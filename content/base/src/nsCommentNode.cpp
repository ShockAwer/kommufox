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
 * The Original Code is Mozilla Communicator client code.
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
#include "nsIDOMComment.h"
#include "nsGenericDOMDataNode.h"
#include "nsLayoutAtoms.h"
#include "nsCOMPtr.h"
#include "nsIDocument.h"
#include "nsContentUtils.h"


class nsCommentNode : public nsGenericDOMDataNode,
                      public nsIDOMComment
{
public:
  nsCommentNode(nsNodeInfoManager *aNodeInfoManager);
  virtual ~nsCommentNode();

  // nsISupports
  NS_DECL_ISUPPORTS_INHERITED

  // nsIDOMNode
  NS_IMPL_NSIDOMNODE_USING_GENERIC_DOM_DATA

  // nsIDOMCharacterData
  NS_FORWARD_NSIDOMCHARACTERDATA(nsGenericDOMDataNode::)

  // nsIDOMComment
  // Empty interface

  // nsIContent
  virtual nsIAtom *Tag() const;
  virtual PRBool MayHaveFrame() const;
  virtual PRBool IsContentOfType(PRUint32 aFlags) const;

#ifdef DEBUG
  virtual void List(FILE* out, PRInt32 aIndent) const;
  virtual void DumpContent(FILE* out = stdout, PRInt32 aIndent = 0,
                           PRBool aDumpAll = PR_TRUE) const
  {
    return;
  }
#endif

  virtual already_AddRefed<nsITextContent> CloneContent(PRBool aCloneText,
                                                        nsNodeInfoManager *aNodeInfoManager);
};

nsresult
NS_NewCommentNode(nsIContent** aInstancePtrResult,
                  nsNodeInfoManager *aNodeInfoManager)
{
  NS_PRECONDITION(aNodeInfoManager, "Missing nodeinfo manager");

  *aInstancePtrResult = nsnull;

  nsCommentNode *instance = new nsCommentNode(aNodeInfoManager);
  if (!instance) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  NS_ADDREF(*aInstancePtrResult = instance);

  return NS_OK;
}

nsCommentNode::nsCommentNode(nsNodeInfoManager *aNodeInfoManager)
  : nsGenericDOMDataNode(aNodeInfoManager)
{
}

nsCommentNode::~nsCommentNode()
{
}


// QueryInterface implementation for nsCommentNode
NS_INTERFACE_MAP_BEGIN(nsCommentNode)
  NS_INTERFACE_MAP_ENTRY(nsITextContent)
  NS_INTERFACE_MAP_ENTRY(nsIDOMNode)
  NS_INTERFACE_MAP_ENTRY(nsIDOMCharacterData)
  NS_INTERFACE_MAP_ENTRY(nsIDOMComment)
  NS_INTERFACE_MAP_ENTRY_CONTENT_CLASSINFO(Comment)
NS_INTERFACE_MAP_END_INHERITING(nsGenericDOMDataNode)


NS_IMPL_ADDREF_INHERITED(nsCommentNode, nsGenericDOMDataNode)
NS_IMPL_RELEASE_INHERITED(nsCommentNode, nsGenericDOMDataNode)


nsIAtom *
nsCommentNode::Tag() const
{
  return nsLayoutAtoms::commentTagName;
}

// virtual
PRBool
nsCommentNode::MayHaveFrame() const
{
  return PR_FALSE;
}

PRBool
nsCommentNode::IsContentOfType(PRUint32 aFlags) const
{
  return !(aFlags & ~eCOMMENT);
}

NS_IMETHODIMP
nsCommentNode::GetNodeName(nsAString& aNodeName)
{
  aNodeName.AssignLiteral("#comment");
  return NS_OK;
}

NS_IMETHODIMP
nsCommentNode::GetNodeValue(nsAString& aNodeValue)
{
  return nsGenericDOMDataNode::GetNodeValue(aNodeValue);
}

NS_IMETHODIMP
nsCommentNode::SetNodeValue(const nsAString& aNodeValue)
{
  return nsGenericDOMDataNode::SetNodeValue(aNodeValue);
}

NS_IMETHODIMP
nsCommentNode::GetNodeType(PRUint16* aNodeType)
{
  *aNodeType = (PRUint16)nsIDOMNode::COMMENT_NODE;
  return NS_OK;
}

NS_IMETHODIMP
nsCommentNode::CloneNode(PRBool aDeep, nsIDOMNode** aReturn)
{
  nsCOMPtr<nsITextContent> textContent = CloneContent(PR_TRUE,
                                                      mNodeInfoManager);
  NS_ENSURE_TRUE(textContent, NS_ERROR_OUT_OF_MEMORY);

  return CallQueryInterface(textContent, aReturn);
}

already_AddRefed<nsITextContent>
nsCommentNode::CloneContent(PRBool aCloneText,
                            nsNodeInfoManager *aNodeInfoManager)
{
  nsCommentNode* it = new nsCommentNode(aNodeInfoManager);
  if (!it)
    return nsnull;

  if (aCloneText) {
    it->mText = mText;
  }

  NS_ADDREF(it);

  return it;
}

#ifdef DEBUG
void
nsCommentNode::List(FILE* out, PRInt32 aIndent) const
{
  NS_PRECONDITION(IsInDoc(), "bad content");

  PRInt32 indx;
  for (indx = aIndent; --indx >= 0; ) fputs("  ", out);

  fprintf(out, "Comment@%p refcount=%d<!--", this, mRefCnt.get());

  nsAutoString tmp;
  ToCString(tmp, 0, mText.GetLength());
  fputs(NS_LossyConvertUCS2toASCII(tmp).get(), out);

  fputs("-->\n", out);
}
#endif
