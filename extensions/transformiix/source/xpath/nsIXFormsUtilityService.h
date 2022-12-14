/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
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
 * The Original Code is Mozilla XForms support.
 *
 * The Initial Developer of the Original Code is
 * IBM Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2004
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *  Aaron Reed <aaronr@us.ibm.com>
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

#ifndef nsIXFormsUtilityService_h
#define nsIXFormsUtilityService_h


#include "nsISupports.h"

/* For IDL files that don't want to include root IDL files. */
#ifndef NS_NO_VTABLE
#define NS_NO_VTABLE
#endif
class nsIDOMNode; /* forward declaration */

class nsIXFormsModelElement; /* forward declaration */


/* starting interface:    nsIXFormsUtilityService */
#define NS_IXFORMSUTILITYSERVICE_IID_STR "1e51f8fb-22e1-4d5d-a5f3-ab34e76efe8e"

#define NS_IXFORMSUTILITYSERVICE_IID \
  {0x1e51f8fb, 0x22e1, 0x4d5d, \
    { 0xa5, 0xf3, 0xab, 0x34, 0xe7, 0x6e, 0xfe, 0x8e }}

#define NS_XFORMS_UTILITY_CONTRACTID   "@mozilla.org/xforms-utility-service;1"

/* Use this macro when declaring classes that implement this interface. */
#define NS_DECL_NSIXFORMSUTILITYSERVICE \
  NS_IMETHOD GetModelFromNode(nsIDOMNode *node, nsIDOMNode **_retval); \
  NS_IMETHOD IsNodeAssocWithModel(nsIDOMNode *aNode, nsIDOMNode *aModel, PRBool *_retval); \
  NS_IMETHOD GetInstanceDocumentRoot(const nsAString & aID, nsIDOMNode *aModelNode, nsIDOMNode **_retval); \
  NS_IMETHOD ValidateString(const nsAString & aValue, const nsAString & aType, const nsAString & aNamespace, PRBool *_retval); \
  NS_IMETHOD GetRepeatIndexById(nsIDOMNode *aResolverNode, const nsAString &aId, PRInt32 *aIndex); \
  NS_IMETHOD GetMonths(const nsAString & aValue, PRInt32 *aMonths); \
  NS_IMETHOD GetSeconds(const nsAString & aValue, double *aSeconds); \
  NS_IMETHOD GetSecondsFromDateTime(const nsAString & aValue, double *aSeconds); \
  NS_IMETHOD GetDaysFromDateTime(const nsAString & aValue, PRInt32 *aDays);

/**
 * Private interface implemented by the nsXFormsUtilityService in XForms extension.
 *   Defining it here to prevent XPath requiring XForms extension.
 */
class NS_NO_VTABLE nsIXFormsUtilityService : public nsISupports {
 public: 

  NS_DEFINE_STATIC_IID_ACCESSOR(NS_IXFORMSUTILITYSERVICE_IID)

  /**
   * Function to get the corresponding model element from a xforms node or
   * a xforms instance data node.
   */
  /* nsIDOMNode getModelFromNode (in nsIDOMNode node); */
  NS_IMETHOD GetModelFromNode(nsIDOMNode *node, nsIDOMNode **_retval) = 0;

  /**
   * Function to see if the given node is associated with the given model.
   * Right now this function is only called by XPath in the case of the
   * instance() function.
   * The provided node can be an instance node from an instance
   * document and thus be associated to the model in that way (model elements
   * contain instance elements).  Otherwise the node will be an XForms element
   * that was used as the context node of the XPath expression (i.e the
   * XForms control has an attribute that contains an XPath expression).
   * Form controls are associated with model elements either explicitly through
   * single-node binding or implicitly (if model cannot by calculated, it
   * will use the first model element encountered in the document).  The model
   * can also be inherited from a containing element like xforms:group or
   * xforms:repeat.
   */
  /* PRBool isNodeAssocWithModel (in nsIDOMNode aNode, in nsIDOMNode aModel); */
  NS_IMETHOD IsNodeAssocWithModel(nsIDOMNode *aNode, nsIDOMNode *aModel, PRBool *_retval) = 0;

  /**
   * Function to get the instance document root for the instance element with
   * the given id.  The instance element must be associated with the given
   * model.
   */
  /* nsIDOMNode getInstanceDocumentRoot (in DOMString aID, in nsIDOMNode aModelNode); */
  NS_IMETHOD GetInstanceDocumentRoot(const nsAString & aID, nsIDOMNode *aModelNode, nsIDOMNode **_retval) = 0;

  /**
   * Function to ensure that aValue is of the schema type aType.  Will basically
   * be a forwarder to the nsISchemaValidator function of the same name.
   */
  /* boolean validateString (in AString aValue, in AString aType, in AString aNamespace); */
  NS_IMETHOD ValidateString(const nsAString & aValue, const nsAString & aType, const nsAString & aNamespace, PRBool *_retval) = 0;

  /**
   * Function to retrieve the index from the repeat element with the given id.
   */
  /* unsigned long getRepeatIndexById (in nsIDOMNode aResolverNode, in AString aRepeat); */
  NS_IMETHOD GetRepeatIndexById(nsIDOMNode *aResolverNode, const nsAString &aId, PRInt32 *aIndex) = 0;

  /**
   * Function to retrieve the number of months represented by the 
   * xsd:duration provided in aValue
   */
  /* long getMonths (in DOMString aValue); */
  NS_IMETHOD GetMonths(const nsAString & aValue, PRInt32 *aMonths) = 0;

  /**
   * Function to retrieve the number of seconds represented by the 
   * xsd:duration provided in aValue
   */
  /* AString getSeconds (in DOMString aValue); */
  NS_IMETHOD GetSeconds(const nsAString & aValue, double *aSeconds) = 0;

  /**
   * Function to retrieve the number of seconds represented by the 
   * xsd:dateTime provided in aValue
   */
  /* AString getSecondsFromDateTime (in DOMString aValue); */
  NS_IMETHOD GetSecondsFromDateTime(const nsAString & aValue, 
                                    double *aSeconds) = 0;

  /**
   * Function to retrieve the number of days represented by the 
   * xsd:dateTime provided in aValue
   */
  /* AString getDaysFromDateTime (in DOMString aValue); */
  NS_IMETHOD GetDaysFromDateTime(const nsAString & aValue, 
                                 PRInt32         * aDays) = 0;
};

#define NS_ERROR_XFORMS_CALCUATION_EXCEPTION \
                       NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_GENERAL, 3001)

#endif /* nsIXFormsUtilityService_h */
