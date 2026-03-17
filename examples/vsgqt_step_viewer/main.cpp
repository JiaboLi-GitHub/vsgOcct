#include <vsgocct/StepModelLoader.h>
#include <vsgocct/selection/ScenePicker.h>

#include <QtCore/QFileInfo>
#include <QtCore/QStringList>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFrame>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include <vsgQt/Viewer.h>
#include <vsgQt/Window.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>

namespace
{
QString resolveStepFile(const QStringList& arguments)
{
    if (arguments.size() > 1)
    {
        return arguments.at(1);
    }

    const QString initialDirectory = std::filesystem::exists("D:/OCCT/data/step")
                                         ? QStringLiteral("D:/OCCT/data/step")
                                         : QString();

    return QFileDialog::getOpenFileName(
        nullptr,
        QStringLiteral("Open STEP Model"),
        initialDirectory,
        QStringLiteral("STEP Files (*.step *.stp *.STEP *.STP);;All Files (*.*)"));
}

QString partLabel(const vsgocct::scene::PartSceneNode& part, int fallbackIndex)
{
    QString label = QString::fromStdString(part.name);
    if (label.isEmpty())
    {
        label = QStringLiteral("(unnamed #%1)").arg(fallbackIndex);
    }
    return label;
}

QString defaultStatusMessage(const vsgocct::scene::AssemblySceneData& sceneData)
{
    return QStringLiteral("Parts: %1 | Triangles: %2 | Lines: %3 | Points: %4")
        .arg(static_cast<qulonglong>(sceneData.parts.size()))
        .arg(static_cast<qulonglong>(sceneData.totalTriangleCount))
        .arg(static_cast<qulonglong>(sceneData.totalLineSegmentCount))
        .arg(static_cast<qulonglong>(sceneData.totalPointCount));
}

QString primitiveKindLabel(vsgocct::selection::PrimitiveKind kind)
{
    using vsgocct::selection::PrimitiveKind;

    switch (kind)
    {
    case PrimitiveKind::Part:
        return QStringLiteral("Part");
    case PrimitiveKind::Face:
        return QStringLiteral("Face");
    case PrimitiveKind::Edge:
        return QStringLiteral("Edge");
    case PrimitiveKind::Vertex:
        return QStringLiteral("Vertex");
    case PrimitiveKind::None:
    default:
        return QStringLiteral("None");
    }
}

struct RenderWindowContext
{
    vsgQt::Window* window = nullptr;
    vsg::ref_ptr<vsg::Camera> camera;
    vsg::ref_ptr<vsg::Trackball> trackball;
};

RenderWindowContext createRenderWindow(
    const vsg::ref_ptr<vsgQt::Viewer>& viewer,
    const vsg::ref_ptr<vsg::WindowTraits>& traits,
    const vsgocct::scene::AssemblySceneData& sceneData)
{
    auto* window = new vsgQt::Window(viewer, traits, static_cast<QWindow*>(nullptr));
    window->setTitle(QString::fromStdString(traits->windowTitle));
    window->initializeWindow();

    if (!traits->device)
    {
        traits->device = window->windowAdapter->getOrCreateDevice();
    }

    const auto width = traits->width;
    const auto height = traits->height;
    const auto radius = std::max(sceneData.radius, 1.0);
    const auto centre = sceneData.center;

    auto lookAt = vsg::LookAt::create(
        centre + vsg::dvec3(0.0, -radius * 3.0, radius * 1.6),
        centre,
        vsg::dvec3(0.0, 0.0, 1.0));
    auto projection = vsg::Perspective::create(
        35.0,
        static_cast<double>(width) / static_cast<double>(height),
        std::max(radius * 0.001, 0.001),
        radius * 12.0);
    auto camera = vsg::Camera::create(projection, lookAt, vsg::ViewportState::create(VkExtent2D{width, height}));

    auto trackball = vsg::Trackball::create(camera);
    trackball->addWindow(*window);

    auto commandGraph = vsg::createCommandGraphForView(*window, camera, sceneData.scene);
    viewer->addRecordAndSubmitTaskAndPresentation({commandGraph});

    return {window, camera, trackball};
}

class SelectionClickHandler : public vsg::Inherit<vsg::Visitor, SelectionClickHandler>
{
public:
    SelectionClickHandler(vsg::ref_ptr<vsgQt::Viewer> viewer,
                          vsg::ref_ptr<vsg::Camera> camera,
                          vsgocct::scene::AssemblySceneData& sceneData,
                          QStatusBar* statusBar)
        : viewer_(std::move(viewer)),
          camera_(std::move(camera)),
          sceneData_(sceneData),
          statusBar_(statusBar)
    {
    }

    void apply(vsg::ButtonPressEvent& buttonPress) override
    {
        if (buttonPress.button != 1)
        {
            return;
        }

        pressed_ = true;
        pressX_ = buttonPress.x;
        pressY_ = buttonPress.y;
    }

    void apply(vsg::ButtonReleaseEvent& buttonRelease) override
    {
        if (buttonRelease.button != 1 || !pressed_)
        {
            return;
        }

        pressed_ = false;
        const int deltaX = buttonRelease.x - pressX_;
        const int deltaY = buttonRelease.y - pressY_;
        const double dragDistance = std::sqrt(static_cast<double>(deltaX * deltaX + deltaY * deltaY));
        if (dragDistance > 3.0)
        {
            return;
        }

        const auto pickResult = vsgocct::selection::pick(*camera_, sceneData_, buttonRelease.x, buttonRelease.y);
        if (!pickResult)
        {
            vsgocct::scene::clearSelection(sceneData_);
            showDefaultStatus();
            requestRefresh();
            return;
        }

        vsgocct::scene::setSelection(sceneData_, pickResult->token);
        showPickResult(*pickResult);
        requestRefresh();
    }

    void showDefaultStatus() const
    {
        if (statusBar_)
        {
            statusBar_->showMessage(defaultStatusMessage(sceneData_));
        }
    }

    void clearSelection()
    {
        vsgocct::scene::clearSelection(sceneData_);
        showDefaultStatus();
        requestRefresh();
    }

    void requestRefresh() const
    {
        if (viewer_)
        {
            viewer_->request();
        }
    }

private:
    void showPickResult(const vsgocct::selection::PickResult& pickResult) const
    {
        if (!statusBar_)
        {
            return;
        }

        const auto* part = vsgocct::scene::findPart(sceneData_, pickResult.token.partId);
        const QString label = part
                                  ? partLabel(*part, static_cast<int>(pickResult.token.partId + 1))
                                  : QStringLiteral("(unknown part)");
        statusBar_->showMessage(
            QStringLiteral("Selected %1 %2 | %3 | Hit (%4, %5, %6)")
                .arg(primitiveKindLabel(pickResult.token.kind))
                .arg(static_cast<qulonglong>(pickResult.token.primitiveId))
                .arg(label)
                .arg(pickResult.worldIntersection.x, 0, 'f', 3)
                .arg(pickResult.worldIntersection.y, 0, 'f', 3)
                .arg(pickResult.worldIntersection.z, 0, 'f', 3));
    }

    vsg::ref_ptr<vsgQt::Viewer> viewer_;
    vsg::ref_ptr<vsg::Camera> camera_;
    vsgocct::scene::AssemblySceneData& sceneData_;
    QStatusBar* statusBar_ = nullptr;
    bool pressed_ = false;
    int pressX_ = 0;
    int pressY_ = 0;
};

class OverlayPositioner : public QObject
{
public:
    OverlayPositioner(QWidget* overlay, QWidget* anchor, const QPoint& offset)
        : QObject(anchor), overlay_(overlay), anchor_(anchor), offset_(offset) {}

protected:
    bool eventFilter(QObject* /*watched*/, QEvent* event) override
    {
        switch (event->type())
        {
        case QEvent::Move:
        case QEvent::Resize:
        case QEvent::Show:
            overlay_->move(anchor_->mapToGlobal(offset_));
            break;
        case QEvent::WindowStateChange:
            if (anchor_->isMinimized())
                overlay_->hide();
            else if (anchor_->isVisible())
            {
                overlay_->show();
                overlay_->move(anchor_->mapToGlobal(offset_));
            }
            break;
        default:
            break;
        }
        return false;
    }

private:
    QWidget* overlay_;
    QWidget* anchor_;
    QPoint offset_;
};
} // namespace

int main(int argc, char* argv[])
{
    try
    {
        QApplication application(argc, argv);

        const QString stepFile = resolveStepFile(application.arguments());
        if (stepFile.isEmpty())
        {
            return 0;
        }

        auto sceneData = vsgocct::loadStepScene(std::filesystem::path(stepFile.toStdWString()));
        std::cout << "Loaded " << sceneData.totalPointCount << " points, "
                  << sceneData.totalLineSegmentCount << " line segments, "
                  << sceneData.totalTriangleCount << " triangles, "
                  << sceneData.parts.size() << " parts from "
                  << stepFile.toLocal8Bit().constData() << std::endl;

        auto viewer = vsgQt::Viewer::create();

        auto traits = vsg::WindowTraits::create();
        traits->windowTitle = "vsgQt OCCT STEP Viewer";
        traits->width = 1280;
        traits->height = 900;
        traits->samples = VK_SAMPLE_COUNT_4_BIT;

        QMainWindow mainWindow;
        auto renderWindowContext = createRenderWindow(viewer, traits, sceneData);

        auto* centralBase = new QWidget(&mainWindow);
        auto* layout = new QVBoxLayout(centralBase);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        auto* container = QWidget::createWindowContainer(renderWindowContext.window);
        container->setFocusPolicy(Qt::StrongFocus);
        layout->addWidget(container);

        mainWindow.setWindowTitle(QStringLiteral("STEP Viewer - %1").arg(QFileInfo(stepFile).fileName()));
        mainWindow.setCentralWidget(centralBase);
        mainWindow.resize(static_cast<int>(traits->width), static_cast<int>(traits->height));

        auto selectionHandler = vsg::ref_ptr<SelectionClickHandler>(
            new SelectionClickHandler(viewer, renderWindowContext.camera, sceneData, mainWindow.statusBar()));
        viewer->addEventHandler(selectionHandler);
        viewer->addEventHandler(renderWindowContext.trackball);
        viewer->addEventHandler(vsg::CloseHandler::create(viewer));

        auto* overlay = new QWidget(&mainWindow, Qt::Tool | Qt::FramelessWindowHint);
        overlay->setAttribute(Qt::WA_TranslucentBackground);
        overlay->setStyleSheet(QStringLiteral(R"(
            QWidget#overlayPanel {
                background-color: rgba(30, 30, 30, 180);
                border-radius: 8px;
                border: 1px solid rgba(255, 255, 255, 30);
            }
            QCheckBox {
                color: rgba(255, 255, 255, 180);
                font-size: 12px;
                padding: 3px 6px;
            }
            QCheckBox::indicator {
                width: 14px;
                height: 14px;
            }
            QLabel#partListTitle {
                color: rgba(255, 255, 255, 120);
                font-size: 11px;
                font-weight: bold;
                padding: 2px 6px;
            }
        )"));

        auto* panel = new QWidget(overlay);
        panel->setObjectName(QStringLiteral("overlayPanel"));
        auto* overlayRoot = new QVBoxLayout(overlay);
        overlayRoot->setContentsMargins(0, 0, 0, 0);
        overlayRoot->addWidget(panel);

        auto* panelLayout = new QVBoxLayout(panel);
        panelLayout->setContentsMargins(8, 6, 8, 6);
        panelLayout->setSpacing(2);

        auto* title = new QLabel(QStringLiteral("Parts (%1)").arg(sceneData.parts.size()), panel);
        title->setObjectName(QStringLiteral("partListTitle"));
        panelLayout->addWidget(title);

        auto* scrollArea = new QScrollArea(panel);
        scrollArea->setWidgetResizable(true);
        scrollArea->setFrameShape(QFrame::NoFrame);
        scrollArea->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; }"));

        auto* scrollContent = new QWidget(scrollArea);
        auto* scrollLayout = new QVBoxLayout(scrollContent);
        scrollLayout->setContentsMargins(0, 0, 0, 0);
        scrollLayout->setSpacing(2);

        int partIndex = 0;
        for (const auto& part : sceneData.parts)
        {
            ++partIndex;
            auto* checkbox = new QCheckBox(partLabel(part, partIndex), scrollContent);
            checkbox->setChecked(true);

            auto switchNode = part.switchNode;
            const uint32_t partId = part.partId;
            QObject::connect(checkbox, &QCheckBox::toggled, &mainWindow,
                [switchNode, &sceneData, selectionHandler, partId](bool visible)
                {
                    if (switchNode && !switchNode->children.empty())
                    {
                        switchNode->children.front().mask = visible ? vsg::MASK_ALL : vsg::MASK_OFF;
                    }

                    if (!visible && sceneData.selectedPartId == partId)
                    {
                        selectionHandler->clearSelection();
                    }
                    else
                    {
                        selectionHandler->requestRefresh();
                    }
                });

            scrollLayout->addWidget(checkbox);
        }
        scrollLayout->addStretch();

        scrollArea->setWidget(scrollContent);
        scrollArea->setMaximumHeight(350);
        panelLayout->addWidget(scrollArea);

        overlay->adjustSize();

        const QPoint overlayOffset(12, 12);
        mainWindow.installEventFilter(new OverlayPositioner(overlay, &mainWindow, overlayOffset));
        selectionHandler->showDefaultStatus();

        viewer->continuousUpdate = true;
        viewer->setInterval(16);
        viewer->compile();

        mainWindow.show();

        overlay->move(mainWindow.mapToGlobal(overlayOffset));
        overlay->show();

        return application.exec();
    }
    catch (const vsg::Exception& ex)
    {
        std::cerr << "VSG exception: " << ex.message << " (result=" << ex.result << ')' << std::endl;
        QMessageBox::critical(nullptr, QStringLiteral("VSG Error"), QString::fromLocal8Bit(ex.message.c_str()));
        return 1;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Unhandled exception: " << ex.what() << std::endl;
        QMessageBox::critical(nullptr, QStringLiteral("STEP Viewer Error"), QString::fromLocal8Bit(ex.what()));
        return 1;
    }
}
