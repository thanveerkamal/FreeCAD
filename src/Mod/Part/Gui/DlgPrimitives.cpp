/***************************************************************************
 *   Copyright (c) 2007 Werner Mayer <wmayer[at]users.sourceforge.net>     *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/


#include "PreCompiled.h"
#ifndef _PreComp_
#include <gp_Ax1.hxx>
#include <gp_Ax3.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <Geom_Circle.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <Python.h>
#include <QMessageBox>
#include <Inventor/SoPickedPoint.h>
#include <Inventor/events/SoMouseButtonEvent.h>
#endif

#include <CXX/WrapPython.h>
#include <Base/Interpreter.h>
#include <Base/Rotation.h>
#include <Base/Tools.h>
#include <Base/UnitsApi.h>
#include <App/Application.h>
#include <App/Document.h>
#include <Gui/Application.h>
#include <Gui/Document.h>
#include <Gui/Command.h>
#include <Gui/View3DInventor.h>
#include <Gui/View3DInventorViewer.h>
#include <Gui/SoFCUnifiedSelection.h>
#include <Gui/TaskView/TaskView.h>
#include <Mod/Part/App/PrimitiveFeature.h>
#include <Mod/Part/App/FeaturePartBox.h>
#include <Mod/Part/App/FeaturePartCircle.h>
#include <Mod/Part/App/Tools.h>

#include "DlgPrimitives.h"

using namespace PartGui;

namespace PartGui {

const char* gce_ErrorStatusText(gce_ErrorType et)
{
    switch (et)
    {
    case gce_Done:
        return "Construction was successful";
    case gce_ConfusedPoints:
        return "Two points are coincident";
    case gce_NegativeRadius:
        return "Radius value is negative";
    case gce_ColinearPoints:
        return "Three points are collinear";
    case gce_IntersectionError:
        return "Intersection cannot be computed";
    case gce_NullAxis:
        return "Axis is undefined";
    case gce_NullAngle:
        return "Angle value is invalid (usually null)";
    case gce_NullRadius:
        return "Radius is null";
    case gce_InvertAxis:
        return "Axis value is invalid";
    case gce_BadAngle:
        return "Angle value is invalid";
    case gce_InvertRadius:
        return "Radius value is incorrect (usually with respect to another radius)";
    case gce_NullFocusLength:
        return "Focal distance is null";
    case gce_NullVector:
        return "Vector is null";
    case gce_BadEquation:
        return "Coefficients are incorrect (applies to the equation of a geometric object)";
    default:
        return "Creation of geometry failed";
    }
}

void Picker::createPrimitive(QWidget* widget, const QString& descr, Gui::Document* doc)
{
    try {
        App::Document* app = doc->getDocument();
        QString cmd = this->command(app);

        // Execute the Python block
        doc->openCommand(descr.toUtf8());
        Gui::Command::runCommand(Gui::Command::Doc, cmd.toLatin1());
        doc->commitCommand();
        Gui::Command::runCommand(Gui::Command::Doc, "App.ActiveDocument.recompute()");
        Gui::Command::runCommand(Gui::Command::Gui, "Gui.SendMsgToActiveView(\"ViewFit\")");
    }
    catch (const Base::Exception& e) {
        QMessageBox::warning(widget, descr, QString::fromLatin1(e.what()));
    }
}

QString Picker::toPlacement(const gp_Ax2& axis) const
{
    gp_Dir dir = axis.Direction();
    gp_Pnt pnt = gp_Pnt(0.0,0.0,0.0);
    gp_Ax3 ax3(pnt, dir, axis.XDirection());

    gp_Trsf Trf;
    Trf.SetTransformation(ax3);
    Trf.Invert();

    gp_XYZ theAxis(0,0,1);
    Standard_Real theAngle = 0.0;
    Trf.GetRotation(theAxis,theAngle);

    Base::Rotation rot(Base::convertTo<Base::Vector3d>(theAxis), theAngle);
    gp_Pnt loc = axis.Location();

    return QString::fromLatin1("Base.Placement(Base.Vector(%1,%2,%3),Base.Rotation(%4,%5,%6,%7))")
        .arg(loc.X(),0,'f',Base::UnitsApi::getDecimals())
        .arg(loc.Y(),0,'f',Base::UnitsApi::getDecimals())
        .arg(loc.Z(),0,'f',Base::UnitsApi::getDecimals())
        .arg(rot[0],0,'f',Base::UnitsApi::getDecimals())
        .arg(rot[1],0,'f',Base::UnitsApi::getDecimals())
        .arg(rot[2],0,'f',Base::UnitsApi::getDecimals())
        .arg(rot[3],0,'f',Base::UnitsApi::getDecimals());
}

class CircleFromThreePoints : public Picker
{
public:
    CircleFromThreePoints() : Picker()
    {
    }
    bool pickedPoint(const SoPickedPoint * point)
    {
        SbVec3f pnt = point->getPoint();
        points.push_back(gp_Pnt(pnt[0],pnt[1],pnt[2]));
        return points.size() == 3;
    }
    QString command(App::Document* doc) const
    {
        GC_MakeArcOfCircle arc(points[0], points[1], points[2]);
        if (!arc.IsDone())
            throw Base::CADKernelError(gce_ErrorStatusText(arc.Status()));
        Handle(Geom_TrimmedCurve) trim = arc.Value();
        Handle(Geom_Circle) circle = Handle(Geom_Circle)::DownCast(trim->BasisCurve());

        QString name = QString::fromLatin1(doc->getUniqueObjectName("Circle").c_str());
        return QString::fromLatin1(
            "App.ActiveDocument.addObject(\"Part::Circle\",\"%1\")\n"
            "App.ActiveDocument.%1.Radius=%2\n"
            "App.ActiveDocument.%1.Angle0=%3\n"
            "App.ActiveDocument.%1.Angle1=%4\n"
            "App.ActiveDocument.%1.Placement=%5\n")
            .arg(name)
            .arg(circle->Radius(),0,'f',Base::UnitsApi::getDecimals())
            .arg(Base::toDegrees(trim->FirstParameter()),0,'f',Base::UnitsApi::getDecimals())
            .arg(Base::toDegrees(trim->LastParameter ()),0,'f',Base::UnitsApi::getDecimals())
            .arg(toPlacement(circle->Position()));
    }

private:
    std::vector<gp_Pnt> points;
};

}

/* TRANSLATOR PartGui::DlgPrimitives */

DlgPrimitives::DlgPrimitives(QWidget* parent, Part::Primitive* feature)
  : QWidget(parent)
  , featurePtr(feature)
{
    ui.setupUi(this);
    Gui::Command::doCommand(Gui::Command::Doc, "from FreeCAD import Base");
    Gui::Command::doCommand(Gui::Command::Doc, "import Part,PartGui");

    // set limits
    //
    // plane
    ui.planeLength->setRange(0, INT_MAX);
    ui.planeWidth->setRange(0, INT_MAX);
    // box
    ui.boxLength->setRange(0, INT_MAX);
    ui.boxWidth->setRange(0, INT_MAX);
    ui.boxHeight->setRange(0, INT_MAX);
    // cylinder
    ui.cylinderRadius->setRange(0, INT_MAX);
    ui.cylinderHeight->setRange(0, INT_MAX);
    ui.cylinderAngle->setRange(0, 360);
    // cone
    ui.coneRadius1->setRange(0, INT_MAX);
    ui.coneRadius2->setRange(0, INT_MAX);
    ui.coneHeight->setRange(0, INT_MAX);
    ui.coneAngle->setRange(0, 360);
    // sphere
    ui.sphereRadius->setRange(0, INT_MAX);
    ui.sphereAngle1->setRange(-90, 90);
    ui.sphereAngle2->setRange(-90, 90);
    ui.sphereAngle3->setRange(0, 360);
    // ellipsoid
    ui.ellipsoidRadius1->setRange(0, INT_MAX);
    ui.ellipsoidRadius2->setRange(0, INT_MAX);
    ui.ellipsoidRadius3->setRange(0, INT_MAX);
    ui.ellipsoidAngle1->setRange(-90, 90);
    ui.ellipsoidAngle2->setRange(-90, 90);
    ui.ellipsoidAngle3->setRange(0, 360);
    // torus
    ui.torusRadius1->setRange(0, INT_MAX);
    ui.torusRadius2->setRange(0, INT_MAX);
    ui.torusAngle1->setRange(-180, 180);
    ui.torusAngle2->setRange(-180, 180);
    ui.torusAngle3->setRange(0, 360);
    // prism
    ui.prismCircumradius->setRange(0, INT_MAX);
    ui.prismHeight->setRange(0, INT_MAX);
    // wedge
    ui.wedgeXmin->setMinimum(INT_MIN);
    ui.wedgeXmin->setMaximum(INT_MAX);
    ui.wedgeYmin->setMinimum(INT_MIN);
    ui.wedgeYmin->setMaximum(INT_MAX);
    ui.wedgeZmin->setMinimum(INT_MIN);
    ui.wedgeZmin->setMaximum(INT_MAX);
    ui.wedgeX2min->setMinimum(INT_MIN);
    ui.wedgeX2min->setMaximum(INT_MAX);
    ui.wedgeZ2min->setMinimum(INT_MIN);
    ui.wedgeZ2min->setMaximum(INT_MAX);
    ui.wedgeXmax->setMinimum(INT_MIN);
    ui.wedgeXmax->setMaximum(INT_MAX);
    ui.wedgeYmax->setMinimum(INT_MIN);
    ui.wedgeYmax->setMaximum(INT_MAX);
    ui.wedgeZmax->setMinimum(INT_MIN);
    ui.wedgeZmax->setMaximum(INT_MAX);
    ui.wedgeX2max->setMinimum(INT_MIN);
    ui.wedgeX2max->setMaximum(INT_MAX);
    ui.wedgeZ2max->setMinimum(INT_MIN);
    ui.wedgeZ2max->setMaximum(INT_MAX);
    // helix
    ui.helixPitch->setRange(0, INT_MAX);
    ui.helixHeight->setRange(0, INT_MAX);
    ui.helixRadius->setRange(0, INT_MAX);
    ui.helixAngle->setRange(0, 90);
    // circle
    ui.circleRadius->setRange(0, INT_MAX);
    ui.circleAngle0->setRange(0, 360);
    ui.circleAngle1->setRange(0, 360);
    // ellipse
    ui.ellipseMajorRadius->setRange(0, INT_MAX);
    ui.ellipseMinorRadius->setRange(0, INT_MAX);
    ui.ellipseAngle0->setRange(0, 360);
    ui.ellipseAngle1->setRange(0, 360);
    // vertex
    ui.vertexX->setMaximum(INT_MAX);
    ui.vertexY->setMaximum(INT_MAX);
    ui.vertexZ->setMaximum(INT_MAX);
    ui.vertexX->setMinimum(INT_MIN);
    ui.vertexY->setMinimum(INT_MIN);
    ui.vertexZ->setMinimum(INT_MIN);
    // line
    ui.edgeX1->setMaximum(INT_MAX);
    ui.edgeX1->setMinimum(INT_MIN);
    ui.edgeY1->setMaximum(INT_MAX);
    ui.edgeY1->setMinimum(INT_MIN);
    ui.edgeZ1->setMaximum(INT_MAX);
    ui.edgeZ1->setMinimum(INT_MIN);
    ui.edgeX2->setMaximum(INT_MAX);
    ui.edgeX2->setMinimum(INT_MIN);
    ui.edgeY2->setMaximum(INT_MAX);
    ui.edgeY2->setMinimum(INT_MIN);
    ui.edgeZ2->setMaximum(INT_MAX);
    ui.edgeZ2->setMinimum(INT_MIN);
    // RegularPolygon
    ui.regularPolygonCircumradius->setRange(0, INT_MAX);

    // fill the dialog with data if the primitives already exists
    if (feature) {
        // must be the same order as of the stacked widget
        std::vector<Base::Type> types;
        types.emplace_back(Part::Plane::getClassTypeId());
        types.emplace_back(Part::Box::getClassTypeId());
        types.emplace_back(Part::Cylinder::getClassTypeId());
        types.emplace_back(Part::Cone::getClassTypeId());
        types.emplace_back(Part::Sphere::getClassTypeId());
        types.emplace_back(Part::Ellipsoid::getClassTypeId());
        types.emplace_back(Part::Torus::getClassTypeId());
        types.emplace_back(Part::Prism::getClassTypeId());
        types.emplace_back(Part::Wedge::getClassTypeId());
        types.emplace_back(Part::Helix::getClassTypeId());
        types.emplace_back(Part::Spiral::getClassTypeId());
        types.emplace_back(Part::Circle::getClassTypeId());
        types.emplace_back(Part::Ellipse::getClassTypeId());
        types.emplace_back(Part::Vertex::getClassTypeId());
        types.emplace_back(Part::Line::getClassTypeId());
        types.emplace_back(Part::RegularPolygon::getClassTypeId());

        Base::Type type = feature->getTypeId();
        int index = std::distance(types.begin(), std::find(types.begin(), types.end(), type));
        ui.comboBox1->setCurrentIndex(index);
        ui.widgetStack2->setCurrentIndex(index);

        // if existing, the primitive type can not be changed by the user
        ui.comboBox1->setDisabled(feature != nullptr);

        // ToDo: connect signal if there is a preview of primitives available
        // read values from the properties
        if (type == Part::Plane::getClassTypeId()) {
            Part::Plane* plane = static_cast<Part::Plane*>(feature);
            ui.planeLength->setValue(plane->Length.getQuantityValue());
            ui.planeWidth->setValue(plane->Width.getQuantityValue());
        }
        else if (type == Part::Box::getClassTypeId()) {
            Part::Box* box = static_cast<Part::Box*>(feature);
            ui.boxLength->setValue(box->Length.getQuantityValue());
            ui.boxWidth->setValue(box->Width.getQuantityValue());
            ui.boxHeight->setValue(box->Height.getQuantityValue());
        }
        else if (type == Part::Cylinder::getClassTypeId()) {
            Part::Cylinder* cyl = static_cast<Part::Cylinder*>(feature);
            ui.cylinderRadius->setValue(cyl->Radius.getQuantityValue());
            ui.cylinderHeight->setValue(cyl->Height.getQuantityValue());
            ui.cylinderAngle->setValue(cyl->Angle.getQuantityValue());
        }
        else if (type == Part::Cone::getClassTypeId()) {
            Part::Cone* cone = static_cast<Part::Cone*>(feature);
            ui.coneRadius1->setValue(cone->Radius1.getQuantityValue());
            ui.coneRadius2->setValue(cone->Radius2.getQuantityValue());
            ui.coneHeight->setValue(cone->Height.getQuantityValue());
            ui.coneAngle->setValue(cone->Angle.getQuantityValue());
        }
        else if (type == Part::Sphere::getClassTypeId()) {
            Part::Sphere* sphere = static_cast<Part::Sphere*>(feature);
            ui.sphereRadius->setValue(sphere->Radius.getQuantityValue());
            ui.sphereAngle1->setValue(sphere->Angle1.getQuantityValue());
            ui.sphereAngle2->setValue(sphere->Angle2.getQuantityValue());
            ui.sphereAngle3->setValue(sphere->Angle3.getQuantityValue());
        }
        else if (type == Part::Ellipsoid::getClassTypeId()) {
            Part::Ellipsoid* ell = static_cast<Part::Ellipsoid*>(feature);
            ui.ellipsoidRadius1->setValue(ell->Radius1.getQuantityValue());
            ui.ellipsoidRadius2->setValue(ell->Radius2.getQuantityValue());
            ui.ellipsoidRadius3->setValue(ell->Radius3.getQuantityValue());
            ui.ellipsoidAngle1->setValue(ell->Angle1.getQuantityValue());
            ui.ellipsoidAngle2->setValue(ell->Angle2.getQuantityValue());
            ui.ellipsoidAngle3->setValue(ell->Angle3.getQuantityValue());
        }
        else if (type == Part::Torus::getClassTypeId()) {
            Part::Torus* torus = static_cast<Part::Torus*>(feature);
            ui.torusRadius1->setValue(torus->Radius1.getQuantityValue());
            ui.torusRadius2->setValue(torus->Radius2.getQuantityValue());
            ui.torusAngle1->setValue(torus->Angle1.getQuantityValue());
            ui.torusAngle2->setValue(torus->Angle2.getQuantityValue());
            ui.torusAngle3->setValue(torus->Angle3.getQuantityValue());
        }
        else if (type == Part::Prism::getClassTypeId()) {
            Part::Prism* prism = static_cast<Part::Prism*>(feature);
            ui.prismPolygon->setValue(prism->Polygon.getValue());
            ui.prismCircumradius->setValue(prism->Circumradius.getQuantityValue());
            ui.prismHeight->setValue(prism->Height.getQuantityValue());
            ui.prismXSkew->setValue(prism->FirstAngle.getQuantityValue());
            ui.prismYSkew->setValue(prism->SecondAngle.getQuantityValue());
        }
        else if (type == Part::Wedge::getClassTypeId()) {
            Part::Wedge* wedge = static_cast<Part::Wedge*>(feature);
            ui.wedgeXmin->setValue(wedge->Xmin.getQuantityValue());
            ui.wedgeYmin->setValue(wedge->Ymin.getQuantityValue());
            ui.wedgeZmin->setValue(wedge->Zmin.getQuantityValue());
            ui.wedgeX2min->setValue(wedge->X2min.getQuantityValue());
            ui.wedgeZ2min->setValue(wedge->Z2min.getQuantityValue());
            ui.wedgeXmax->setValue(wedge->Xmax.getQuantityValue());
            ui.wedgeYmax->setValue(wedge->Ymax.getQuantityValue());
            ui.wedgeZmax->setValue(wedge->Zmax.getQuantityValue());
            ui.wedgeX2max->setValue(wedge->X2max.getQuantityValue());
            ui.wedgeZ2max->setValue(wedge->Z2max.getQuantityValue());
        }
        else if (type == Part::Helix::getClassTypeId()) {
            Part::Helix* helix = static_cast<Part::Helix*>(feature);
            ui.helixPitch->setValue(helix->Pitch.getQuantityValue());
            ui.helixHeight->setValue(helix->Height.getQuantityValue());
            ui.helixRadius->setValue(helix->Radius.getQuantityValue());
            ui.helixAngle->setValue(helix->Angle.getQuantityValue());
            ui.helixLocalCS->setCurrentIndex(helix->LocalCoord.getValue());
        }
        else if (type == Part::Spiral::getClassTypeId()) {
            Part::Spiral* spiral = static_cast<Part::Spiral*>(feature);
            ui.spiralGrowth->setValue(spiral->Growth.getQuantityValue());
            ui.spiralRotation->setValue(spiral->Rotations.getQuantityValue().getValue());
            ui.spiralRadius->setValue(spiral->Radius.getQuantityValue());
        }
        else if (type == Part::Circle::getClassTypeId()) {
            Part::Circle* circle = static_cast<Part::Circle*>(feature);
            ui.circleRadius->setValue(circle->Radius.getQuantityValue());
            ui.circleAngle0->setValue(circle->Angle0.getQuantityValue());
            ui.circleAngle1->setValue(circle->Angle1.getQuantityValue());
        }
        else if (type == Part::Ellipse::getClassTypeId()) {
            Part::Ellipse* ell = static_cast<Part::Ellipse*>(feature);
            ui.ellipseMajorRadius->setValue(ell->MajorRadius.getQuantityValue());
            ui.ellipseMinorRadius->setValue(ell->MinorRadius.getQuantityValue());
            ui.ellipseAngle0->setValue(ell->Angle0.getQuantityValue());
            ui.ellipseAngle1->setValue(ell->Angle1.getQuantityValue());
        }
        else if (type == Part::Vertex::getClassTypeId()) {
            Part::Vertex* v = static_cast<Part::Vertex*>(feature);
            ui.vertexX->setValue(v->X.getQuantityValue());
            ui.vertexY->setValue(v->Y.getQuantityValue());
            ui.vertexZ->setValue(v->Z.getQuantityValue());
        }
        else if (type == Part::Line::getClassTypeId()) {
            Part::Line* line = static_cast<Part::Line*>(feature);
            ui.edgeX1->setValue(line->X1.getQuantityValue());
            ui.edgeY1->setValue(line->Y1.getQuantityValue());
            ui.edgeZ1->setValue(line->Z1.getQuantityValue());
            ui.edgeX2->setValue(line->X2.getQuantityValue());
            ui.edgeY2->setValue(line->Y2.getQuantityValue());
            ui.edgeZ2->setValue(line->Z2.getQuantityValue());
        }
        else if (type == Part::RegularPolygon::getClassTypeId()) {
            Part::RegularPolygon* poly = static_cast<Part::RegularPolygon*>(feature);
            ui.regularPolygonPolygon->setValue(poly->Polygon.getValue());
            ui.regularPolygonCircumradius->setValue(poly->Circumradius.getQuantityValue());
        }
    }
}

/*  
 *  Destroys the object and frees any allocated resources
 */
DlgPrimitives::~DlgPrimitives()
{
}

void DlgPrimitives::pickCallback(void * ud, SoEventCallback * n)
{
    const SoMouseButtonEvent * mbe = static_cast<const SoMouseButtonEvent*>(n->getEvent());
    Picker* pick = reinterpret_cast<Picker*>(ud);
    if (pick->exitCode >= 0)
        pick->loop.exit(pick->exitCode);

    // Mark all incoming mouse button events as handled, especially, to deactivate the selection node
    n->setHandled();
    if (mbe->getButton() == SoMouseButtonEvent::BUTTON1) {
        if (mbe->getState() == SoButtonEvent::DOWN) {
            const SoPickedPoint * point = n->getPickedPoint();
            if (point) {
                if (pick->pickedPoint(point)) {
                    pick->exitCode = 0;
                }
            }
        }
    }
    else if (mbe->getButton() == SoMouseButtonEvent::BUTTON2) {
        if (mbe->getState() == SoButtonEvent::UP) {
            pick->loop.exit(1);
        }
    }
}

void DlgPrimitives::executeCallback(Picker* p)
{
    Gui::Document* doc = Gui::Application::Instance->activeDocument();
    if (!doc) {
        return;
    }

    Gui::View3DInventor* view = static_cast<Gui::View3DInventor*>(doc->getActiveView());
    if (view) {
        Gui::View3DInventorViewer* viewer = view->getViewer();
        if (!viewer->isEditing()) {
            viewer->setEditing(true);
            viewer->setRedirectToSceneGraph(true);
            SoNode* root = viewer->getSceneGraph();
            int mode = 0;
            if (root && root->getTypeId().isDerivedFrom(Gui::SoFCUnifiedSelection::getClassTypeId())) {
                mode = static_cast<Gui::SoFCUnifiedSelection*>(root)->selectionMode.getValue();
                static_cast<Gui::SoFCUnifiedSelection*>(root)->selectionMode.setValue(Gui::SoFCUnifiedSelection::OFF);
            }
            viewer->addEventCallback(SoMouseButtonEvent::getClassTypeId(), pickCallback, p);
            this->setDisabled(true);
            int ret = p->loop.exec();
            if (root && root->getTypeId().isDerivedFrom(Gui::SoFCUnifiedSelection::getClassTypeId()))
                static_cast<Gui::SoFCUnifiedSelection*>(root)->selectionMode.setValue(mode);
            this->setEnabled(true);
            viewer->setEditing(false);
            viewer->setRedirectToSceneGraph(false);
            viewer->removeEventCallback(SoMouseButtonEvent::getClassTypeId(), pickCallback, p);

            if (ret == 0) {
                p->createPrimitive(this, ui.comboBox1->currentText(), doc);
            }
        }
    }
}

void DlgPrimitives::on_buttonCircleFromThreePoints_clicked()
{
    CircleFromThreePoints pp;
    executeCallback(&pp);
}

void DlgPrimitives::createPrimitive(const QString& placement)
{
    try {
        QString cmd; QString name;
        App::Document* doc = App::GetApplication().getActiveDocument();
        if (!doc) {
            QMessageBox::warning(this, tr("Create %1")
                .arg(ui.comboBox1->currentText()), tr("No active document"));
            return;
        }
        if (ui.comboBox1->currentIndex() == 0) {         // plane
            name = QString::fromLatin1(doc->getUniqueObjectName("Plane").c_str());
            cmd = QString::fromLatin1(
                "App.ActiveDocument.addObject(\"Part::Plane\",\"%1\")\n"
                "App.ActiveDocument.%1.Length=%2\n"
                "App.ActiveDocument.%1.Width=%3\n"
                "App.ActiveDocument.%1.Placement=%4\n"
                "App.ActiveDocument.%1.Label='%5'\n")
                .arg(name)
                .arg(ui.planeLength->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.planeWidth->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(placement)
                .arg(tr("Plane"));
        }
        else if (ui.comboBox1->currentIndex() == 1) {         // box
            name = QString::fromLatin1(doc->getUniqueObjectName("Box").c_str());
            cmd = QString::fromLatin1(
                "App.ActiveDocument.addObject(\"Part::Box\",\"%1\")\n"
                "App.ActiveDocument.%1.Length=%2\n"
                "App.ActiveDocument.%1.Width=%3\n"
                "App.ActiveDocument.%1.Height=%4\n"
                "App.ActiveDocument.%1.Placement=%5\n"
                "App.ActiveDocument.%1.Label='%6'\n")
                .arg(name)
                .arg(ui.boxLength->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.boxWidth->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.boxHeight->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(placement)
                .arg(tr("Box"));
        }
        else if (ui.comboBox1->currentIndex() == 2) {  // cylinder
            name = QString::fromLatin1(doc->getUniqueObjectName("Cylinder").c_str());
            cmd = QString::fromLatin1(
                "App.ActiveDocument.addObject(\"Part::Cylinder\",\"%1\")\n"
                "App.ActiveDocument.%1.Radius=%2\n"
                "App.ActiveDocument.%1.Height=%3\n"
                "App.ActiveDocument.%1.Angle=%4\n"
                "App.ActiveDocument.%1.Placement=%5\n"
                "App.ActiveDocument.%1.Label='%6'\n")
                .arg(name)
                .arg(ui.cylinderRadius->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.cylinderHeight->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.cylinderAngle->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(placement)
                .arg(tr("Cylinder"));
        }
        else if (ui.comboBox1->currentIndex() == 3) {  // cone
            name = QString::fromLatin1(doc->getUniqueObjectName("Cone").c_str());
            cmd = QString::fromLatin1(
                "App.ActiveDocument.addObject(\"Part::Cone\",\"%1\")\n"
                "App.ActiveDocument.%1.Radius1=%2\n"
                "App.ActiveDocument.%1.Radius2=%3\n"
                "App.ActiveDocument.%1.Height=%4\n"
                "App.ActiveDocument.%1.Angle=%5\n"
                "App.ActiveDocument.%1.Placement=%6\n"
                "App.ActiveDocument.%1.Label='%7'\n")
                .arg(name)
                .arg(ui.coneRadius1->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.coneRadius2->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.coneHeight->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.coneAngle->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(placement)
                .arg(tr("Cone"));
        }
        else if (ui.comboBox1->currentIndex() == 4) {  // sphere
            name = QString::fromLatin1(doc->getUniqueObjectName("Sphere").c_str());
            cmd = QString::fromLatin1(
                "App.ActiveDocument.addObject(\"Part::Sphere\",\"%1\")\n"
                "App.ActiveDocument.%1.Radius=%2\n"
                "App.ActiveDocument.%1.Angle1=%3\n"
                "App.ActiveDocument.%1.Angle2=%4\n"
                "App.ActiveDocument.%1.Angle3=%5\n"
                "App.ActiveDocument.%1.Placement=%6\n"
                "App.ActiveDocument.%1.Label='%7'\n")
                .arg(name)
                .arg(ui.sphereRadius->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.sphereAngle1->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.sphereAngle2->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.sphereAngle3->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(placement)
                .arg(tr("Sphere"));
        }
        else if (ui.comboBox1->currentIndex() == 5) {  // ellipsoid
            name = QString::fromLatin1(doc->getUniqueObjectName("Ellipsoid").c_str());
            cmd = QString::fromLatin1(
                "App.ActiveDocument.addObject(\"Part::Ellipsoid\",\"%1\")\n"
                "App.ActiveDocument.%1.Radius1=%2\n"
                "App.ActiveDocument.%1.Radius2=%3\n"
                "App.ActiveDocument.%1.Radius3=%4\n"
                "App.ActiveDocument.%1.Angle1=%5\n"
                "App.ActiveDocument.%1.Angle2=%6\n"
                "App.ActiveDocument.%1.Angle3=%7\n"
                "App.ActiveDocument.%1.Placement=%8\n"
                "App.ActiveDocument.%1.Label='%9'\n")
                .arg(name)
                .arg(ui.ellipsoidRadius1->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.ellipsoidRadius2->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.ellipsoidRadius3->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.ellipsoidAngle1->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.ellipsoidAngle2->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.ellipsoidAngle3->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(placement)
                .arg(tr("Ellipsoid"));
        }
        else if (ui.comboBox1->currentIndex() == 6) {  // torus
            name = QString::fromLatin1(doc->getUniqueObjectName("Torus").c_str());
            cmd = QString::fromLatin1(
                "App.ActiveDocument.addObject(\"Part::Torus\",\"%1\")\n"
                "App.ActiveDocument.%1.Radius1=%2\n"
                "App.ActiveDocument.%1.Radius2=%3\n"
                "App.ActiveDocument.%1.Angle1=%4\n"
                "App.ActiveDocument.%1.Angle2=%5\n"
                "App.ActiveDocument.%1.Angle3=%6\n"
                "App.ActiveDocument.%1.Placement=%7\n"
                "App.ActiveDocument.%1.Label='%8'\n")
                .arg(name)
                .arg(ui.torusRadius1->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.torusRadius2->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.torusAngle1->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.torusAngle2->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.torusAngle3->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(placement)
                .arg(tr("Torus"));
        }
        else if (ui.comboBox1->currentIndex() == 7) {  // prism
            name = QString::fromLatin1(doc->getUniqueObjectName("Prism").c_str());
            cmd = QString::fromLatin1(
                "App.ActiveDocument.addObject(\"Part::Prism\",\"%1\")\n"
                "App.ActiveDocument.%1.Polygon=%2\n"
                "App.ActiveDocument.%1.Circumradius=%3\n"
                "App.ActiveDocument.%1.Height=%4\n"
                "App.ActiveDocument.%1.FirstAngle=%5\n"
                "App.ActiveDocument.%1.SecondAngle=%6\n"
                "App.ActiveDocument.%1.Placement=%7\n"
                "App.ActiveDocument.%1.Label='%8'\n")
                .arg(name)
                .arg(ui.prismPolygon->value())
                .arg(ui.prismCircumradius->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.prismHeight->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.prismXSkew->value().getValue(), 0, 'f', Base::UnitsApi::getDecimals())
                .arg(ui.prismYSkew->value().getValue(), 0, 'f', Base::UnitsApi::getDecimals())
                .arg(placement)
                .arg(tr("Prism"));
        }
        else if (ui.comboBox1->currentIndex() == 8) {  // wedge
            name = QString::fromLatin1(doc->getUniqueObjectName("Wedge").c_str());
            cmd = QString::fromLatin1(
                "App.ActiveDocument.addObject(\"Part::Wedge\",\"%1\")\n"
                "App.ActiveDocument.%1.Xmin=%2\n"
                "App.ActiveDocument.%1.Ymin=%3\n"
                "App.ActiveDocument.%1.Zmin=%4\n"
                "App.ActiveDocument.%1.X2min=%5\n"
                "App.ActiveDocument.%1.Z2min=%6\n"
                "App.ActiveDocument.%1.Xmax=%7\n"
                "App.ActiveDocument.%1.Ymax=%8\n"
                "App.ActiveDocument.%1.Zmax=%9\n"
                "App.ActiveDocument.%1.X2max=%10\n"
                "App.ActiveDocument.%1.Z2max=%11\n"
                "App.ActiveDocument.%1.Placement=%12\n"
                "App.ActiveDocument.%1.Label='%13'\n")
                .arg(name)
                .arg(ui.wedgeXmin->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.wedgeYmin->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.wedgeZmin->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.wedgeX2min->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.wedgeZ2min->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.wedgeXmax->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.wedgeYmax->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.wedgeZmax->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.wedgeX2max->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.wedgeZ2max->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(placement)
                .arg(tr("Wedge"));
        }
        else if (ui.comboBox1->currentIndex() == 9) {  // helix
            name = QString::fromLatin1(doc->getUniqueObjectName("Helix").c_str());
            cmd = QString::fromLatin1(
                "App.ActiveDocument.addObject(\"Part::Helix\",\"%1\")\n"
                "App.ActiveDocument.%1.Pitch=%2\n"
                "App.ActiveDocument.%1.Height=%3\n"
                "App.ActiveDocument.%1.Radius=%4\n"
                "App.ActiveDocument.%1.Angle=%5\n"
                "App.ActiveDocument.%1.LocalCoord=%6\n"
                "App.ActiveDocument.%1.Style=1\n"
                "App.ActiveDocument.%1.Placement=%7\n"
                "App.ActiveDocument.%1.Label='%8'\n")
                .arg(name)
                .arg(ui.helixPitch->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.helixHeight->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.helixRadius->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.helixAngle->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.helixLocalCS->currentIndex())
                .arg(placement)
                .arg(tr("Helix"));
        }
        else if (ui.comboBox1->currentIndex() == 10) {  // spiral
            name = QString::fromLatin1(doc->getUniqueObjectName("Spiral").c_str());
            cmd = QString::fromLatin1(
                "App.ActiveDocument.addObject(\"Part::Spiral\",\"%1\")\n"
                "App.ActiveDocument.%1.Growth=%2\n"
                "App.ActiveDocument.%1.Rotations=%3\n"
                "App.ActiveDocument.%1.Radius=%4\n"
                "App.ActiveDocument.%1.Placement=%5\n"
                "App.ActiveDocument.%1.Label='%6'\n")
                .arg(name)
                .arg(ui.spiralGrowth->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.spiralRotation->value(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.spiralRadius->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(placement)
                .arg(tr("Spiral"));
        }
        else if (ui.comboBox1->currentIndex() == 11) {  // circle
            name = QString::fromLatin1(doc->getUniqueObjectName("Circle").c_str());
            cmd = QString::fromLatin1(
                "App.ActiveDocument.addObject(\"Part::Circle\",\"%1\")\n"
                "App.ActiveDocument.%1.Radius=%2\n"
                "App.ActiveDocument.%1.Angle0=%3\n"
                "App.ActiveDocument.%1.Angle1=%4\n"
                "App.ActiveDocument.%1.Placement=%5\n"
                "App.ActiveDocument.%1.Label='%6'\n")
                .arg(name)
                .arg(ui.circleRadius->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.circleAngle0->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.circleAngle1->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(placement)
                .arg(tr("Circle"));
        }
        else if (ui.comboBox1->currentIndex() == 12) {  // ellipse
            name = QString::fromLatin1(doc->getUniqueObjectName("Ellipse").c_str());
            cmd = QString::fromLatin1(
                "App.ActiveDocument.addObject(\"Part::Ellipse\",\"%1\")\n"
                "App.ActiveDocument.%1.MajorRadius=%2\n"
                "App.ActiveDocument.%1.MinorRadius=%3\n"
                "App.ActiveDocument.%1.Angle0=%4\n"
                "App.ActiveDocument.%1.Angle1=%5\n"
                "App.ActiveDocument.%1.Placement=%6\n"
                "App.ActiveDocument.%1.Label='%7'\n")
                .arg(name)
                .arg(ui.ellipseMajorRadius->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.ellipseMinorRadius->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.ellipseAngle0->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.ellipseAngle1->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(placement)
                .arg(tr("Ellipse"));
        }
        else if (ui.comboBox1->currentIndex() == 13) {  // vertex
            name = QString::fromLatin1(doc->getUniqueObjectName("Vertex").c_str());
            cmd = QString::fromLatin1(
                "App.ActiveDocument.addObject(\"Part::Vertex\",\"%1\")\n"
                "App.ActiveDocument.%1.X=%2\n"
                "App.ActiveDocument.%1.Y=%3\n"
                "App.ActiveDocument.%1.Z=%4\n"
                "App.ActiveDocument.%1.Placement=%5\n"
                "App.ActiveDocument.%1.Label='%6'\n")
                .arg(name)
                .arg(ui.vertexX->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.vertexY->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.vertexZ->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(placement)
                .arg(tr("Vertex"));
        }
        else if (ui.comboBox1->currentIndex() == 14) {  // line
            name = QString::fromLatin1(doc->getUniqueObjectName("Line").c_str());
            cmd = QString::fromLatin1(
                "App.ActiveDocument.addObject(\"Part::Line\",\"%1\")\n"
                "App.ActiveDocument.%1.X1=%2\n"
                "App.ActiveDocument.%1.Y1=%3\n"
                "App.ActiveDocument.%1.Z1=%4\n"
                "App.ActiveDocument.%1.X2=%5\n"
                "App.ActiveDocument.%1.Y2=%6\n"
                "App.ActiveDocument.%1.Z2=%7\n"
                "App.ActiveDocument.%1.Placement=%8\n"
                "App.ActiveDocument.%1.Label='%9'\n")
                .arg(name)
                .arg(ui.edgeX1->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.edgeY1->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.edgeZ1->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.edgeX2->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.edgeY2->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(ui.edgeZ2->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(placement)
                .arg(tr("Line"));
        }
        else if (ui.comboBox1->currentIndex() == 15) {  // RegularPolygon
            name = QString::fromLatin1(doc->getUniqueObjectName("RegularPolygon").c_str());
            cmd = QString::fromLatin1(
                "App.ActiveDocument.addObject(\"Part::RegularPolygon\",\"%1\")\n"
                "App.ActiveDocument.%1.Polygon=%2\n"
                "App.ActiveDocument.%1.Circumradius=%3\n"
                "App.ActiveDocument.%1.Placement=%4\n"
                "App.ActiveDocument.%1.Label='%5'\n")
                .arg(name)
                .arg(ui.regularPolygonPolygon->value())
                .arg(ui.regularPolygonCircumradius->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
                .arg(placement)
                .arg(tr("Regular polygon"));
        }

        // Execute the Python block
        QString prim = tr("Create %1").arg(ui.comboBox1->currentText());
        Gui::Application::Instance->activeDocument()->openCommand(prim.toUtf8());
        Gui::Command::runCommand(Gui::Command::Doc, cmd.toUtf8());
        Gui::Application::Instance->activeDocument()->commitCommand();
        Gui::Command::runCommand(Gui::Command::Doc, "App.ActiveDocument.recompute()");
        Gui::Command::runCommand(Gui::Command::Gui, "Gui.SendMsgToActiveView(\"ViewFit\")");
    }
    catch (const Base::PyException& e) {
        QMessageBox::warning(this, tr("Create %1")
            .arg(ui.comboBox1->currentText()), QString::fromLatin1(e.what()));
    }
}

void DlgPrimitives::accept(const QString& placement)
{
    if (featurePtr.expired())
        return;
    QString command;
    App::Document* doc = featurePtr->getDocument();
    Base::Type type = featurePtr->getTypeId();
    QString objectName = QString::fromLatin1("App.getDocument(\"%1\").%2")
                         .arg(QString::fromLatin1(doc->getName()))
                         .arg(QString::fromLatin1(featurePtr->getNameInDocument()));

    // the combox with the primitive type is fixed
    // therefore by reading its state we know what we need to change

    // read values from the properties
    if (type == Part::Plane::getClassTypeId()) {
        command = QString::fromLatin1(
            "%1.Length=%2\n"
            "%1.Width=%3\n"
            "%1.Placement=%4\n")
            .arg(objectName)
            .arg(ui.planeLength->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.planeWidth->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(placement);
    }
    else if (type == Part::Box::getClassTypeId()) {
        command = QString::fromLatin1(
            "%1.Length=%2\n"
            "%1.Width=%3\n"
            "%1.Height=%4\n"
            "%1.Placement=%5\n")
            .arg(objectName)
            .arg(ui.boxLength->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.boxWidth->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.boxHeight->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(placement);
    }
    else if (type == Part::Cylinder::getClassTypeId()) {
        command = QString::fromLatin1(
            "%1.Radius=%2\n"
            "%1.Height=%3\n"
            "%1.Angle=%4\n"
            "%1.Placement=%5\n")
            .arg(objectName)
            .arg(ui.cylinderRadius->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.cylinderHeight->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.cylinderAngle->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(placement);
    }
    else if (type == Part::Cone::getClassTypeId()) {
        command = QString::fromLatin1(
            "%1.Radius1=%2\n"
            "%1.Radius2=%3\n"
            "%1.Height=%4\n"
            "%1.Angle=%5\n"
            "%1.Placement=%6\n")
            .arg(objectName)
            .arg(ui.coneRadius1->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.coneRadius2->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.coneHeight->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.coneAngle->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(placement);
    }
    else if (type == Part::Sphere::getClassTypeId()) {
        command = QString::fromLatin1(
            "%1.Radius=%2\n"
            "%1.Angle1=%3\n"
            "%1.Angle2=%4\n"
            "%1.Angle3=%5\n"
            "%1.Placement=%6\n")
            .arg(objectName)
            .arg(ui.sphereRadius->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.sphereAngle1->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.sphereAngle2->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.sphereAngle3->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(placement);
    }
    else if (type == Part::Ellipsoid::getClassTypeId()) {
        command = QString::fromLatin1(
            "%1.Radius1=%2\n"
            "%1.Radius2=%3\n"
            "%1.Radius3=%4\n"
            "%1.Angle1=%5\n"
            "%1.Angle2=%6\n"
            "%1.Angle3=%7\n"
            "%1.Placement=%8\n")
            .arg(objectName)
            .arg(ui.ellipsoidRadius1->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.ellipsoidRadius2->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.ellipsoidRadius3->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.ellipsoidAngle1->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.ellipsoidAngle2->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.ellipsoidAngle3->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(placement);
    }
    else if (type == Part::Torus::getClassTypeId()) {
        command = QString::fromLatin1(
            "%1.Radius1=%2\n"
            "%1.Radius2=%3\n"
            "%1.Angle1=%4\n"
            "%1.Angle2=%5\n"
            "%1.Angle3=%6\n"
            "%1.Placement=%7\n")
            .arg(objectName)
            .arg(ui.torusRadius1->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.torusRadius2->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.torusAngle1->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.torusAngle2->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.torusAngle3->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(placement);
    }
    else if (type == Part::Prism::getClassTypeId()) {
        command = QString::fromLatin1(
            "%1.Polygon=%2\n"
            "%1.Circumradius=%3\n"
            "%1.Height=%4\n"
            "%1.FirstAngle=%5\n"
            "%1.SecondAngle=%6\n"
            "%1.Placement=%7\n")
            .arg(objectName)
            .arg(ui.prismPolygon->value())
            .arg(ui.prismCircumradius->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.prismHeight->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.prismXSkew->value().getValue(), 0, 'f', Base::UnitsApi::getDecimals())
            .arg(ui.prismYSkew->value().getValue(), 0, 'f', Base::UnitsApi::getDecimals())
            .arg(placement);
    }
    else if (type == Part::Wedge::getClassTypeId()) {
        command = QString::fromLatin1(
            "%1.Xmin=%2\n"
            "%1.Ymin=%3\n"
            "%1.Zmin=%4\n"
            "%1.X2min=%5\n"
            "%1.Z2min=%6\n"
            "%1.Xmax=%7\n"
            "%1.Ymax=%8\n"
            "%1.Zmax=%9\n"
            "%1.X2max=%10\n"
            "%1.Z2max=%11\n"
            "%1.Placement=%12\n")
            .arg(objectName)
            .arg(ui.wedgeXmin->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.wedgeYmin->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.wedgeZmin->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.wedgeX2min->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.wedgeZ2min->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.wedgeXmax->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.wedgeYmax->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.wedgeZmax->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.wedgeX2max->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.wedgeZ2max->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(placement);
    }
    else if (type == Part::Helix::getClassTypeId()) {
        command = QString::fromLatin1(
            "%1.Pitch=%2\n"
            "%1.Height=%3\n"
            "%1.Radius=%4\n"
            "%1.Angle=%5\n"
            "%1.LocalCoord=%6\n"
            "%1.Placement=%7\n")
            .arg(objectName)
            .arg(ui.helixPitch->value().getValue(), 0, 'f', Base::UnitsApi::getDecimals())
            .arg(ui.helixHeight->value().getValue(), 0, 'f', Base::UnitsApi::getDecimals())
            .arg(ui.helixRadius->value().getValue(), 0, 'f', Base::UnitsApi::getDecimals())
            .arg(ui.helixAngle->value().getValue(), 0, 'f', Base::UnitsApi::getDecimals())
            .arg(ui.helixLocalCS->currentIndex())
            .arg(placement);
    }
    else if (type == Part::Spiral::getClassTypeId()) {
        command = QString::fromLatin1(
            "%1.Growth=%2\n"
            "%1.Rotations=%3\n"
            "%1.Radius=%4\n"
            "%1.Placement=%5\n")
            .arg(objectName)
            .arg(ui.spiralGrowth->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.spiralRotation->value(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.spiralRadius->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(placement);
    }
    else if (type == Part::Circle::getClassTypeId()) {
        command = QString::fromLatin1(
            "%1.Radius=%2\n"
            "%1.Angle0=%3\n"
            "%1.Angle1=%4\n"
            "%1.Placement=%5\n")
            .arg(objectName)
            .arg(ui.circleRadius->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.circleAngle0->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.circleAngle1->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(placement);
    }
    else if (type == Part::Ellipse::getClassTypeId()) {
        command = QString::fromLatin1(
            "%1.MajorRadius=%2\n"
            "%1.MinorRadius=%3\n"
            "%1.Angle0=%4\n"
            "%1.Angle1=%5\n"
            "%1.Placement=%6\n")
            .arg(objectName)
            .arg(ui.ellipseMajorRadius->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.ellipseMinorRadius->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.ellipseAngle0->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.ellipseAngle1->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(placement);
    }
    else if (type == Part::Vertex::getClassTypeId()) {
        command = QString::fromLatin1(
            "%1.X=%2\n"
            "%1.Y=%3\n"
            "%1.Z=%4\n"
            "%1.Placement=%5\n")
            .arg(objectName)
            .arg(ui.vertexX->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.vertexY->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.vertexZ->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(placement);
    }
    else if (type == Part::Line::getClassTypeId()) {
        command = QString::fromLatin1(
            "%1.X1=%2\n"
            "%1.Y1=%3\n"
            "%1.Z1=%4\n"
            "%1.X2=%5\n"
            "%1.Y2=%6\n"
            "%1.Z2=%7\n"
            "%1.Placement=%8\n")
            .arg(objectName)
            .arg(ui.edgeX1->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.edgeY1->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.edgeZ1->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.edgeX2->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.edgeY2->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(ui.edgeZ2->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(placement);
    }
    else if (type == Part::RegularPolygon::getClassTypeId()) {
        command = QString::fromLatin1(
            "%1.Polygon=%2\n"
            "%1.Circumradius=%3\n"
            "%1.Placement=%4\n")
            .arg(objectName)
            .arg(ui.regularPolygonPolygon->value())
            .arg(ui.regularPolygonCircumradius->value().getValue(),0,'f',Base::UnitsApi::getDecimals())
            .arg(placement);
    }

    // store command for undo
    QString cmd = tr("Edit %1").arg(QString::fromUtf8(featurePtr->Label.getValue()));
    doc->openTransaction(cmd.toLatin1());
    // execute command
    Gui::Command::runCommand(Gui::Command::App, command.toLatin1());
    doc->recompute();
    // commit undo command
    doc->commitTransaction();
}

// ----------------------------------------------

/* TRANSLATOR PartGui::Location */

Location::Location(QWidget* parent, Part::Feature* feature)
{
    Q_UNUSED(parent);
    mode = 0;
    ui.setupUi(this);

    ui.XPositionQSB->setUnit(Base::Unit::Length);
    ui.YPositionQSB->setUnit(Base::Unit::Length);
    ui.ZPositionQSB->setUnit(Base::Unit::Length);
    ui.AngleQSB->setUnit(Base::Unit::Angle);

    // fill location widget if object already exists
    if (feature) {
        // get the placement values
        auto placement = feature->Placement.getValue();

        auto position = placement.getPosition();
        ui.XPositionQSB->setValue(position.x);
        ui.YPositionQSB->setValue(position.y);
        ui.ZPositionQSB->setValue(position.z);

        double rotationAngle;
        Base::Vector3d rotationAxes;
        auto rotation = placement.getRotation();
        rotation.getRawValue(rotationAxes, rotationAngle);
        ui.XDirectionEdit->setValue(rotationAxes.x);
        ui.YDirectionEdit->setValue(rotationAxes.y);
        ui.ZDirectionEdit->setValue(rotationAxes.z);
        // the angle is in this format: 180� = PI, thus transform it to deg
        ui.AngleQSB->setValue(Base::toDegrees<double>(rotationAngle));
    }
}

Location::~Location()
{
    // no need to delete child widgets, Qt does it all for us
    if (!this->activeView.isNull()) {
        Gui::View3DInventorViewer* viewer = static_cast<Gui::View3DInventor*>
            (this->activeView.data())->getViewer();
        viewer->setEditing(false);
        viewer->setRedirectToSceneGraph(false);
        viewer->removeEventCallback(SoMouseButtonEvent::getClassTypeId(), pickCallback,this);
        SoNode* root = viewer->getSceneGraph();
        if (root && root->getTypeId().isDerivedFrom(Gui::SoFCUnifiedSelection::getClassTypeId()))
            static_cast<Gui::SoFCUnifiedSelection*>(root)->selectionMode.setValue(this->mode);
    }
}

void Location::on_viewPositionButton_clicked()
{
    Gui::Document* doc = Gui::Application::Instance->activeDocument();
    if (!doc) {
        return;
    }

    Gui::View3DInventor* view = static_cast<Gui::View3DInventor*>(doc->getActiveView());
    if (view && !this->activeView) {
        Gui::View3DInventorViewer* viewer = view->getViewer();
        if (!viewer->isEditing()) {
            this->activeView = view;
            viewer->setEditing(true);
            viewer->setRedirectToSceneGraph(true);
            viewer->addEventCallback(SoMouseButtonEvent::getClassTypeId(), pickCallback, this);
            SoNode* root = viewer->getSceneGraph();
            if (root && root->getTypeId().isDerivedFrom(Gui::SoFCUnifiedSelection::getClassTypeId())) {
                this->mode = static_cast<Gui::SoFCUnifiedSelection*>(root)->selectionMode.getValue();
                static_cast<Gui::SoFCUnifiedSelection*>(root)->selectionMode.setValue(Gui::SoFCUnifiedSelection::OFF);
            }
        }
     }
}

void Location::pickCallback(void * ud, SoEventCallback * n)
{
    const SoMouseButtonEvent * mbe = static_cast<const SoMouseButtonEvent*>(n->getEvent());
    Gui::View3DInventorViewer* view  = reinterpret_cast<Gui::View3DInventorViewer*>(n->getUserData());

    // Mark all incoming mouse button events as handled, especially, to deactivate the selection node
    n->getAction()->setHandled();
    if (mbe->getButton() == SoMouseButtonEvent::BUTTON1) {
        if (mbe->getState() == SoButtonEvent::DOWN) {
            const SoPickedPoint * point = n->getPickedPoint();
            if (point) {
                SbVec3f pnt = point->getPoint();
                SbVec3f nor = point->getNormal();
                Location* dlg = reinterpret_cast<Location*>(ud);
                dlg->ui.XPositionQSB->setValue(pnt[0]);
                dlg->ui.YPositionQSB->setValue(pnt[1]);
                dlg->ui.ZPositionQSB->setValue(pnt[2]);
                dlg->ui.XDirectionEdit->setValue(nor[0]);
                dlg->ui.YDirectionEdit->setValue(nor[1]);
                dlg->ui.ZDirectionEdit->setValue(nor[2]);
                n->setHandled();
            }
        }
    }
    else if (mbe->getButton() == SoMouseButtonEvent::BUTTON2) {
        if (mbe->getState() == SoButtonEvent::UP) {
            n->setHandled();
            view->setEditing(false);
            view->setRedirectToSceneGraph(false);
            Location* dlg = reinterpret_cast<Location*>(ud);
            dlg->activeView = 0;
            view->removeEventCallback(SoMouseButtonEvent::getClassTypeId(), pickCallback,ud);
            SoNode* root = view->getSceneGraph();
            if (root && root->getTypeId().isDerivedFrom(Gui::SoFCUnifiedSelection::getClassTypeId()))
                static_cast<Gui::SoFCUnifiedSelection*>(root)->selectionMode.setValue(static_cast<Location*>(ud)->mode);
        }
    }
}

QString Location::toPlacement() const
{
    // create a command to set the position and angle of the primitive object

    Base::Vector3d rot;
    rot.x = ui.XDirectionEdit->value();
    rot.y = ui.YDirectionEdit->value();
    rot.z = ui.ZDirectionEdit->value();

    double angle = ui.AngleQSB->rawValue();

    Base::Vector3d loc;
    loc.x = ui.XPositionQSB->rawValue();
    loc.y = ui.YPositionQSB->rawValue();
    loc.z = ui.ZPositionQSB->rawValue();

    return QString::fromLatin1("App.Placement(App.Vector(%1,%2,%3),App.Rotation(App.Vector(%4,%5,%6),%7))")
        .arg(loc.x, 0, 'f', Base::UnitsApi::getDecimals())
        .arg(loc.y, 0, 'f', Base::UnitsApi::getDecimals())
        .arg(loc.z, 0, 'f', Base::UnitsApi::getDecimals())
        .arg(rot.x, 0, 'f', Base::UnitsApi::getDecimals())
        .arg(rot.y, 0, 'f', Base::UnitsApi::getDecimals())
        .arg(rot.z, 0, 'f', Base::UnitsApi::getDecimals())
        .arg(angle, 0, 'f', Base::UnitsApi::getDecimals());
}

// ----------------------------------------------

/* TRANSLATOR PartGui::TaskPrimitives */

TaskPrimitives::TaskPrimitives()
{
    Gui::TaskView::TaskBox* taskbox;
    widget = new DlgPrimitives();
    taskbox = new Gui::TaskView::TaskBox(QPixmap(), widget->windowTitle(), true, 0);
    taskbox->groupLayout()->addWidget(widget);
    Content.push_back(taskbox);

    location = new Location();
    taskbox = new Gui::TaskView::TaskBox(QPixmap(), location->windowTitle() ,true, 0);
    taskbox->groupLayout()->addWidget(location);
    taskbox->hideGroupBox();
    Content.push_back(taskbox);
}

TaskPrimitives::~TaskPrimitives()
{
    // automatically deleted in the sub-class
}

QDialogButtonBox::StandardButtons TaskPrimitives::getStandardButtons() const
{ 
    return QDialogButtonBox::Close|
           QDialogButtonBox::Ok;
}

void TaskPrimitives::modifyStandardButtons(QDialogButtonBox* box)
{
    QPushButton* btn = box->button(QDialogButtonBox::Ok);
    btn->setText(QApplication::translate("PartGui::DlgPrimitives", "&Create"));
}

bool TaskPrimitives::accept()
{
    widget->createPrimitive(location->toPlacement());
    return false;
}

bool TaskPrimitives::reject()
{
    return true;
}

// ----------------------------------------------

/* TRANSLATOR PartGui::TaskPrimitivesEdit */

TaskPrimitivesEdit::TaskPrimitivesEdit(Part::Primitive* feature)
{
    // create and show dialog for the primitives
    Gui::TaskView::TaskBox* taskbox;
    widget = new DlgPrimitives(nullptr, feature);
    taskbox = new Gui::TaskView::TaskBox(QPixmap(), widget->windowTitle(), true, 0);
    taskbox->groupLayout()->addWidget(widget);
    Content.push_back(taskbox);

    // create and show dialog for the location
    location = new Location(nullptr, feature);
    taskbox = new Gui::TaskView::TaskBox(QPixmap(), location->windowTitle(), true, 0);
    taskbox->groupLayout()->addWidget(location);
    Content.push_back(taskbox);
}

TaskPrimitivesEdit::~TaskPrimitivesEdit()
{
    // automatically deleted in the sub-class
}

QDialogButtonBox::StandardButtons TaskPrimitivesEdit::getStandardButtons() const
{
    return QDialogButtonBox::Close |
        QDialogButtonBox::Ok;
}

void TaskPrimitivesEdit::modifyStandardButtons(QDialogButtonBox* box)
{
    QPushButton* btn = box->button(QDialogButtonBox::Ok);
    btn->setText(QApplication::translate("PartGui::DlgPrimitives", "&OK"));
}

bool TaskPrimitivesEdit::accept()
{
    widget->accept(location->toPlacement());
    std::string document = getDocumentName(); // needed because resetEdit() deletes this instance
    Gui::Command::doCommand(Gui::Command::Gui, "Gui.getDocument('%s').resetEdit()", document.c_str());
    return true;
}

bool TaskPrimitivesEdit::reject()
{
    std::string document = getDocumentName(); // needed because resetEdit() deletes this instance
    Gui::Command::doCommand(Gui::Command::Gui, "Gui.getDocument('%s').resetEdit()", document.c_str());
    return true;
}

#include "moc_DlgPrimitives.cpp"
