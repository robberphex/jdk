/*
 * Portions Copyright 2006 Sun Microsystems, Inc.  All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Sun designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Sun in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 */
package com.sun.tools.internal.ws.processor.model.jaxb;

import java.util.List;
import java.util.ArrayList;

import javax.xml.namespace.QName;

import com.sun.tools.internal.xjc.api.Mapping;
import com.sun.tools.internal.xjc.api.Property;
import com.sun.tools.internal.xjc.api.TypeAndAnnotation;
import com.sun.codemodel.internal.JType;

/**
 * @author Kohsuke Kawaguchi, Vivek Pandey
 */
public class JAXBMapping {

    /**
     * @see Mapping#getElement()
     */
    private QName elementName;

    /**
     *
     */
    private JAXBTypeAndAnnotation type;

    /**
     * @see Mapping#getWrapperStyleDrilldown()
     */
    private List<JAXBProperty> wrapperStyleDrilldown;

    /**
     * Default constructor for the persistence.
     */
    public JAXBMapping() {}

    /**
     * Constructor that fills in the values from the given raw model
     */
    JAXBMapping( com.sun.tools.internal.xjc.api.Mapping rawModel ) {
        elementName = rawModel.getElement();
        TypeAndAnnotation typeAndAnno = rawModel.getType();
        type = new JAXBTypeAndAnnotation(typeAndAnno);
        List<? extends Property> list = rawModel.getWrapperStyleDrilldown();
        if(list==null)
            wrapperStyleDrilldown = null;
        else {
            wrapperStyleDrilldown = new ArrayList<JAXBProperty>(list.size());
            for( Property p : list )
                wrapperStyleDrilldown.add(new JAXBProperty(p));
        }

    }

    /**
     * @see Mapping#getElement()
     */
    public QName getElementName() {
        return elementName;
    }

    public void setElementName(QName elementName) {
        this.elementName = elementName;
    }


    public JAXBTypeAndAnnotation getType() {
        return type;
    }

    /**
     * @see Mapping#getWrapperStyleDrilldown()
     */
    public List<JAXBProperty> getWrapperStyleDrilldown() {
        return wrapperStyleDrilldown;
    }
}
