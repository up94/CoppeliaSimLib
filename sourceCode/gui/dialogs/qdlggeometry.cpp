#include <qdlggeometry.h>
#include <ui_qdlggeometry.h>
#include <tt.h>
#include <gV.h>
#include <mesh.h>
#include <simStringTable.h>
#include <app.h>
#include <vMessageBox.h>

CQDlgGeometry::CQDlgGeometry(QWidget *parent) :
    CDlgEx(parent),
    ui(new Ui::CQDlgGeometry)
{
    _dlgType=GEOMETRY_DLG;
    ui->setupUi(this);
    _shapeHandle=-1;
    if (App::mainWindow!=nullptr)
        App::mainWindow->dlgCont->close(GEOMETRY_DLG);
}

CQDlgGeometry::~CQDlgGeometry()
{
    delete ui;
}

void CQDlgGeometry::refresh()
{
    QLineEdit* lineEditToSelect=getSelectedLineEdit();
    bool noEditModeNoSim=(App::getEditModeType()==NO_EDIT_MODE)&&App::currentWorld->simulation->isSimulationStopped();

    if (!isLinkedDataValid())
        return;
    if (!insideRefreshTriggered)
        _setCurrentSizes();
    insideRefreshTriggered=false;
    CShape* shape=App::currentWorld->sceneObjects->getShapeFromHandle(_shapeHandle);
    if (shape==nullptr)
        return;
    bool g=!shape->getMeshWrapper()->isMesh();

    ui->qqSizeX->setEnabled(noEditModeNoSim);
    ui->qqSizeY->setEnabled(noEditModeNoSim);
    ui->qqSizeZ->setEnabled(noEditModeNoSim);
    ui->qqScaleX->setEnabled(noEditModeNoSim);
    ui->qqScaleY->setEnabled(noEditModeNoSim);
    ui->qqScaleZ->setEnabled(noEditModeNoSim);
    ui->qqApplySize->setEnabled(noEditModeNoSim);
    ui->qqApplyScale->setEnabled(noEditModeNoSim);

    ui->qqSizeX->setText(tt::getEString(false,sizeVal[0],4).c_str());
    ui->qqSizeY->setText(tt::getEString(false,sizeVal[1],4).c_str());
    ui->qqSizeZ->setText(tt::getEString(false,sizeVal[2],4).c_str());
    ui->qqScaleX->setText(tt::getFString(true,scaleVal[0],4).c_str());
    ui->qqScaleY->setText(tt::getFString(true,scaleVal[1],4).c_str());
    ui->qqScaleZ->setText(tt::getFString(true,scaleVal[2],4).c_str());
    ui->qqAlpha->setText(tt::getAngleFString(true,rotationVal[0],2).c_str());
    ui->qqBeta->setText(tt::getAngleFString(true,rotationVal[1],2).c_str());
    ui->qqGamma->setText(tt::getAngleFString(true,rotationVal[2],2).c_str());
    bool canScaleFreely=(!g)&&(shape->getMeshWrapper()->getPurePrimitiveType()!=sim_primitiveshape_spheroid)&&(shape->getMeshWrapper()->getPurePrimitiveType()!=sim_primitiveshape_capsule);
    ui->qqKeepProp->setChecked(keepProp||(!canScaleFreely));
    ui->qqKeepProp->setEnabled(canScaleFreely&&noEditModeNoSim);
    ui->qqAlpha->setEnabled(((!isPureShape)||g)&&noEditModeNoSim);
    ui->qqBeta->setEnabled(((!isPureShape)||g)&&noEditModeNoSim);
    ui->qqGamma->setEnabled(((!isPureShape)||g)&&noEditModeNoSim);
    ui->qqApplyEuler->setEnabled(((!isPureShape)||g)&&noEditModeNoSim);
    std::string shapeTypeText;
    if (isPureShape)
    {
        if (g)
            shapeTypeText="Compound primitive shape";
        else
        {
            if (shape->getMeshWrapper()->getPurePrimitiveType()==sim_primitiveshape_heightfield)
                shapeTypeText="Heightfield shape";
            if (shape->getMeshWrapper()->getPurePrimitiveType()==sim_primitiveshape_plane)
                shapeTypeText="Primitive shape (plane)";
            if (shape->getMeshWrapper()->getPurePrimitiveType()==sim_primitiveshape_disc)
                shapeTypeText="Primitive shape (disc)";
            if (shape->getMeshWrapper()->getPurePrimitiveType()==sim_primitiveshape_cuboid)
                shapeTypeText="Primitive shape (cuboid)";
            if (shape->getMeshWrapper()->getPurePrimitiveType()==sim_primitiveshape_spheroid)
                shapeTypeText="Primitive shape (spheroid)";
            if (shape->getMeshWrapper()->getPurePrimitiveType()==sim_primitiveshape_cylinder)
                shapeTypeText="Primitive shape (cylinder)";
            if (shape->getMeshWrapper()->getPurePrimitiveType()==sim_primitiveshape_capsule)
                shapeTypeText="Primitive shape (capsule)";
            if (shape->getMeshWrapper()->getPurePrimitiveType()==sim_primitiveshape_cone)
                shapeTypeText="Primitive shape (cone)";
        }
    }
    else
    {
        if (g)
        {
            if (isConvex)
                shapeTypeText="Compound convex shape";
            else
                shapeTypeText="Compound shape";
        }
        else
        {
            if (isConvex)
                shapeTypeText="Convex shape";
            else
                shapeTypeText="Random shape";
        }
    }
    ui->qqShapeType->setText(shapeTypeText.c_str());

    setWindowTitle(titleText.c_str());
    ui->qqVertexCnt->setText(tt::getIString(false,vertexCount).c_str());
    ui->qqTriangleCnt->setText(tt::getIString(false,triangleCount).c_str());

    selectLineEdit(lineEditToSelect);
}

bool CQDlgGeometry::needsDestruction()
{
    if (!isLinkedDataValid())
        return(true);
    return(CDlgEx::needsDestruction());
}

void CQDlgGeometry::_initialize(int shapeHandle)
{
    _shapeHandle=shapeHandle;
    scaleVal[0]=1.0;
    scaleVal[1]=1.0;
    scaleVal[2]=1.0;
    rotationVal[0]=0.0;
    rotationVal[1]=0.0;
    rotationVal[2]=0.0;
    _setCurrentSizes();
    keepProp=true;
    isPureShape=true;
    isConvex=true;
    isGroup=false;
    CShape* shape=App::currentWorld->sceneObjects->getShapeFromHandle(_shapeHandle);
    if (shape!=nullptr)
    {
        titleText="Geometry associated with '";
        titleText+=shape->getObjectAlias_printPath();
        titleText+="'";
        std::vector<double> wvert;
        std::vector<int> wind;
        shape->getMeshWrapper()->getCumulativeMeshes(wvert,&wind,nullptr);
        vertexCount=(int)wvert.size()/3;
        triangleCount=(int)wind.size()/3;
        isPureShape=shape->getMeshWrapper()->isPure();
        isConvex=shape->getMeshWrapper()->isConvex();
        isGroup=!shape->getMeshWrapper()->isMesh();
    }
    insideRefreshTriggered=true;
    refresh();
}

void CQDlgGeometry::_setCurrentSizes()
{
    CShape* shape=App::currentWorld->sceneObjects->getShapeFromHandle(_shapeHandle);
    if (shape!=nullptr)
    {
        C3Vector bbhalfSizes(shape->getBoundingBoxHalfSizes());
        sizeVal[0]=2.0*bbhalfSizes(0);
        sizeVal[1]=2.0*bbhalfSizes(1);
        sizeVal[2]=2.0*bbhalfSizes(2);
    }
}

bool CQDlgGeometry::isLinkedDataValid()
{
    if (!App::currentWorld->simulation->isSimulationStopped())
        return(false);
    if (App::getEditModeType()!=NO_EDIT_MODE)
        return(false);
    if (App::currentWorld->sceneObjects->getShapeFromHandle(_shapeHandle)!=nullptr)
        return(App::currentWorld->sceneObjects->getLastSelectionHandle()==_shapeHandle);
    return(false);
}

void CQDlgGeometry::display(int shapeHandle,QWidget* theParentWindow)
{
    if (App::mainWindow==nullptr)
        return;
    App::mainWindow->dlgCont->close(GEOMETRY_DLG);
    if (App::mainWindow->dlgCont->openOrBringToFront(GEOMETRY_DLG))
    {
        CQDlgGeometry* geom=(CQDlgGeometry*)App::mainWindow->dlgCont->getDialog(GEOMETRY_DLG);
        if (geom!=nullptr)
            geom->_initialize(shapeHandle);
    }
}

void CQDlgGeometry::cancelEvent()
{ // We just hide the dialog and destroy it at next rendering pass
    _shapeHandle=-1;
    CDlgEx::cancelEvent();
}

bool CQDlgGeometry::doesInstanceSwitchRequireDestruction()
{
    return(true);
}

void CQDlgGeometry::_readSize(int index)
{
    QLineEdit* ww[3]={ui->qqSizeX,ui->qqSizeY,ui->qqSizeZ};
    if (!isLinkedDataValid())
        return;
    CShape* shape=App::currentWorld->sceneObjects->getShapeFromHandle(_shapeHandle);
    if (shape!=nullptr)
    {
        bool ok;
        double newVal=ww[index]->text().toFloat(&ok);
        if (ok)
        {
            newVal=tt::getLimitedFloat(0.0001,1000.0,newVal);
            double sc=1.0;

            C3Vector bbhalfSizes(shape->getBoundingBoxHalfSizes());

            if ((sizeVal[index]!=0.0)&&(bbhalfSizes(index)!=0.0)) // imagine we have a plane that has dims x*y*0!

                sc=newVal/sizeVal[index];
            if (keepProp)
            {
                for (int i=0;i<3;i++)
                    sizeVal[i]*=sc;
            }
            else
            {
                if (bbhalfSizes(index)==0.0)
                    newVal=0.0; // imagine we have a plane that has dims x*y*0!
                sizeVal[index]=newVal;
            }

            if ( shape->getMeshWrapper()->isMesh()&&(shape->getMeshWrapper()->getPurePrimitiveType()!=sim_primitiveshape_spheroid)&&(shape->getMeshWrapper()->getPurePrimitiveType()!=sim_primitiveshape_capsule) )
            {
                if ( (shape->getMeshWrapper()->getPurePrimitiveType()==sim_primitiveshape_disc)||
                    (shape->getMeshWrapper()->getPurePrimitiveType()==sim_primitiveshape_cylinder)||
                    (shape->getMeshWrapper()->getPurePrimitiveType()==sim_primitiveshape_cone)||
                    (shape->getMeshWrapper()->getPurePrimitiveType()==sim_primitiveshape_heightfield) )
                {
                    if (index==0)
                        sizeVal[1]=sizeVal[0];
                    if (index==1)
                        sizeVal[0]=sizeVal[1];
                }
            }
            else
            { // groups, spheres and capsules have only iso-scaling
                if (!keepProp)
                { // should normally never happen (compound shapes have the "keepProp" flag set)
                    if (index==0)
                    {
                        sizeVal[1]*=sc;
                        sizeVal[2]*=sc;
                    }
                    if (index==1)
                    {
                        sizeVal[0]*=sc;
                        sizeVal[2]*=sc;
                    }
                    if (index==2)
                    {
                        sizeVal[0]*=sc;
                        sizeVal[1]*=sc;
                    }
                }
            }
        }
    }
}

void CQDlgGeometry::_readScaling(int index)
{
    QLineEdit* ww[3]={ui->qqScaleX,ui->qqScaleY,ui->qqScaleZ};
    if (!isLinkedDataValid())
        return;
    CShape* shape=App::currentWorld->sceneObjects->getShapeFromHandle(_shapeHandle);
    if (shape!=nullptr)
    {
        bool ok;
        double newVal=ww[index]->text().toFloat(&ok);
        if (!keepProp)
        { // imagine we have a plane that has dims x*y*0!
            C3Vector bbhalfSizes(shape->getBoundingBoxHalfSizes());
            if (bbhalfSizes(index)==0.0)
                newVal=1.0;
        }
        if (ok)
        {
            if ((newVal>=0)||isPureShape||isConvex) // pure or convex shapes should never be flipped!
                newVal=tt::getLimitedFloat(0.0001,1000.0,newVal);
            else
                newVal=tt::getLimitedFloat(-1000.0,-0.0001,newVal);

            double sc=1.0;
            if (scaleVal[index]!=0.0)
                sc=newVal/scaleVal[index];
            if (keepProp)
            {
                for (int i=0;i<3;i++)
                    scaleVal[i]*=sc;
            }
            else
                scaleVal[index]=newVal;

            if ( shape->getMeshWrapper()->isMesh()&&(shape->getMeshWrapper()->getPurePrimitiveType()!=sim_primitiveshape_spheroid)&&(shape->getMeshWrapper()->getPurePrimitiveType()!=sim_primitiveshape_capsule) )
            {
                if ( (shape->getMeshWrapper()->getPurePrimitiveType()==sim_primitiveshape_disc)||
                    (shape->getMeshWrapper()->getPurePrimitiveType()==sim_primitiveshape_cylinder)||
                    (shape->getMeshWrapper()->getPurePrimitiveType()==sim_primitiveshape_cone)||
                    (shape->getMeshWrapper()->getPurePrimitiveType()==sim_primitiveshape_heightfield) )
                {
                    if (index==0)
                        scaleVal[1]=scaleVal[0];
                    if (index==1)
                        scaleVal[0]=scaleVal[1];
                }
            }
            else
            { // groups, spheres and capsules have only iso-scaling
                if (index==0)
                    scaleVal[1]=scaleVal[2]=scaleVal[0];
                if (index==1)
                    scaleVal[0]=scaleVal[2]=scaleVal[1];
                if (index==2)
                    scaleVal[0]=scaleVal[1]=scaleVal[2];
            }
        }
    }
}

void CQDlgGeometry::_readRotation(int index)
{
    QLineEdit* ww[3]={ui->qqAlpha,ui->qqBeta,ui->qqGamma};
    if ((!isLinkedDataValid())||(isPureShape&&(!isGroup)))
        return;
    bool ok;
    double newVal=ww[index]->text().toFloat(&ok);
    if (ok)
    {
        rotationVal[index]=newVal*gv::userToRad;
        C4Vector tr(rotationVal[0],rotationVal[1],rotationVal[2]);
        C3Vector euler(tr.getEulerAngles());
        euler.getData(rotationVal);
    }
}

void CQDlgGeometry::on_qqKeepProp_clicked()
{
    IF_UI_EVENT_CAN_READ_DATA
    {
        if (isLinkedDataValid())
        {
            keepProp=!keepProp;
            scaleVal[0]=1.0;
            scaleVal[1]=1.0;
            scaleVal[2]=1.0;
            _setCurrentSizes(); // to reset sizes
            insideRefreshTriggered=true;
            refresh();
        }
    }
}

void CQDlgGeometry::on_qqSizeX_editingFinished()
{
    if (!ui->qqSizeX->isModified())
        return;
    IF_UI_EVENT_CAN_READ_DATA
    {
        if (isLinkedDataValid())
        {
            _readSize(0);
            insideRefreshTriggered=true;
            refresh();
        }
    }
}

void CQDlgGeometry::on_qqSizeY_editingFinished()
{
    if (!ui->qqSizeY->isModified())
        return;
    IF_UI_EVENT_CAN_READ_DATA
    {
        if (isLinkedDataValid())
        {
            _readSize(1);
            insideRefreshTriggered=true;
            refresh();
        }
    }
}

void CQDlgGeometry::on_qqSizeZ_editingFinished()
{
    if (!ui->qqSizeZ->isModified())
        return;
    IF_UI_EVENT_CAN_READ_DATA
    {
        if (isLinkedDataValid())
        {
            _readSize(2);
            insideRefreshTriggered=true;
            refresh();
        }
    }
}

void CQDlgGeometry::on_qqScaleX_editingFinished()
{
    if (!ui->qqScaleX->isModified())
        return;
    IF_UI_EVENT_CAN_READ_DATA
    {
        if (isLinkedDataValid())
        {
            _readScaling(0);
            insideRefreshTriggered=true;
            refresh();
        }
    }
}

void CQDlgGeometry::on_qqScaleY_editingFinished()
{
    if (!ui->qqScaleY->isModified())
        return;
    IF_UI_EVENT_CAN_READ_DATA
    {
        if (isLinkedDataValid())
        {
            _readScaling(1);
            insideRefreshTriggered=true;
            refresh();
        }
    }
}

void CQDlgGeometry::on_qqScaleZ_editingFinished()
{
    if (!ui->qqScaleZ->isModified())
        return;
    IF_UI_EVENT_CAN_READ_DATA
    {
        if (isLinkedDataValid())
        {
            _readScaling(2);
            insideRefreshTriggered=true;
            refresh();
        }
    }
}

void CQDlgGeometry::on_qqApplySize_clicked()
{
    IF_UI_EVENT_CAN_WRITE_DATA
    {
        if (isLinkedDataValid())
        {
            SSimulationThreadCommand cmd;
            cmd.cmdId=APPLY_SIZE_GEOMETRYGUITRIGGEREDCMD;
            cmd.intParams.push_back(_shapeHandle);
            cmd.floatParams.push_back(sizeVal[0]);
            cmd.floatParams.push_back(sizeVal[1]);
            cmd.floatParams.push_back(sizeVal[2]);
            App::appendSimulationThreadCommand(cmd);
            App::appendSimulationThreadCommand(POST_SCENE_CHANGED_ANNOUNCEMENT_GUITRIGGEREDCMD);
            App::appendSimulationThreadCommand(FULLREFRESH_ALL_DIALOGS_GUITRIGGEREDCMD);
        }
    }
}

void CQDlgGeometry::on_qqApplyScale_clicked()
{
    IF_UI_EVENT_CAN_WRITE_DATA
    {
        if (isLinkedDataValid())
        {
            SSimulationThreadCommand cmd;
            cmd.cmdId=APPLY_SCALING_GEOMETRYGUITRIGGEREDCMD;
            cmd.intParams.push_back(_shapeHandle);
            cmd.floatParams.push_back(scaleVal[0]);
            cmd.floatParams.push_back(scaleVal[1]);
            cmd.floatParams.push_back(scaleVal[2]);
            App::appendSimulationThreadCommand(cmd);
            App::appendSimulationThreadCommand(POST_SCENE_CHANGED_ANNOUNCEMENT_GUITRIGGEREDCMD);
            App::appendSimulationThreadCommand(FULLREFRESH_ALL_DIALOGS_GUITRIGGEREDCMD);
        }
    }
}

void CQDlgGeometry::on_qqAlpha_editingFinished()
{
    if (!ui->qqAlpha->isModified())
        return;
    IF_UI_EVENT_CAN_READ_DATA
    {
        if (isLinkedDataValid())
        {
            _readRotation(0);
            insideRefreshTriggered=true;
            refresh();
        }
    }
}

void CQDlgGeometry::on_qqBeta_editingFinished()
{
    if (!ui->qqBeta->isModified())
        return;
    IF_UI_EVENT_CAN_READ_DATA
    {
        if (isLinkedDataValid())
        {
            _readRotation(1);
            insideRefreshTriggered=true;
            refresh();
        }
    }
}

void CQDlgGeometry::on_qqGamma_editingFinished()
{
    if (!ui->qqGamma->isModified())
        return;
    IF_UI_EVENT_CAN_READ_DATA
    {
        if (isLinkedDataValid())
        {
            _readRotation(2);
            insideRefreshTriggered=true;
            refresh();
        }
    }
}

void CQDlgGeometry::on_qqApplyEuler_clicked()
{
    IF_UI_EVENT_CAN_WRITE_DATA
    {
        if (isLinkedDataValid())
        {
            SSimulationThreadCommand cmd;
            cmd.cmdId=APPLY_FRAMEROTATION_GEOMETRYGUITRIGGEREDCMD;
            cmd.intParams.push_back(_shapeHandle);
            cmd.floatParams.push_back(rotationVal[0]);
            cmd.floatParams.push_back(rotationVal[1]);
            cmd.floatParams.push_back(rotationVal[2]);
            App::appendSimulationThreadCommand(cmd);
            App::appendSimulationThreadCommand(POST_SCENE_CHANGED_ANNOUNCEMENT_GUITRIGGEREDCMD);
            App::appendSimulationThreadCommand(FULLREFRESH_ALL_DIALOGS_GUITRIGGEREDCMD);
        }
    }
}

