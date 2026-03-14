#include <vsgocct/StepModelLoader.h>

#include <QtCore/QFileInfo>
#include <QtCore/QStringList>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
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

vsgQt::Window* createRenderWindow(
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

    viewer->addEventHandler(trackball);
    viewer->addEventHandler(vsg::CloseHandler::create(viewer));

    auto commandGraph = vsg::createCommandGraphForView(*window, camera, sceneData.scene);
    viewer->addRecordAndSubmitTaskAndPresentation({commandGraph});

    return window;
}

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
        auto* renderWindow = createRenderWindow(viewer, traits, sceneData);

        auto* centralBase = new QWidget(&mainWindow);
        auto* layout = new QVBoxLayout(centralBase);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        auto* container = QWidget::createWindowContainer(renderWindow);
        container->setFocusPolicy(Qt::StrongFocus);
        layout->addWidget(container);

        mainWindow.setWindowTitle(QStringLiteral("STEP Viewer - %1").arg(QFileInfo(stepFile).fileName()));
        mainWindow.setCentralWidget(centralBase);
        mainWindow.resize(static_cast<int>(traits->width), static_cast<int>(traits->height));

        // --- Floating overlay: part list with visibility toggles ---
        auto* overlay = new QWidget(&mainWindow, Qt::Tool | Qt::FramelessWindowHint);
        overlay->setAttribute(Qt::WA_TranslucentBackground);
        overlay->setStyleSheet(QStringLiteral(R"(
            QWidget#overlayPanel {
                background-color: rgba(245, 245, 245, 220);
                border-radius: 8px;
                border: 1px solid rgba(0, 0, 0, 40);
            }
            QCheckBox {
                color: rgb(40, 40, 40);
                font-size: 12px;
                padding: 3px 6px;
            }
            QCheckBox::indicator {
                width: 14px;
                height: 14px;
            }
            QLabel#partListTitle {
                color: rgb(80, 80, 80);
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

        // --- Display controls: toggle faces, lines, points ---
        auto* displayTitle = new QLabel(QStringLiteral("Display"), panel);
        displayTitle->setObjectName(QStringLiteral("partListTitle"));
        panelLayout->addWidget(displayTitle);

        auto toggleAllSwitches = [](const std::vector<vsg::ref_ptr<vsg::Switch>>& switches, bool visible)
        {
            for (const auto& sw : switches)
            {
                if (sw && !sw->children.empty())
                {
                    sw->children.front().mask = visible ? vsg::MASK_ALL : vsg::MASK_OFF;
                }
            }
        };

        auto* facesCheckbox = new QCheckBox(QStringLiteral("Faces"), panel);
        facesCheckbox->setChecked(true);
        auto faceSwitches = sceneData.faceSwitches;
        QObject::connect(facesCheckbox, &QCheckBox::toggled, &mainWindow,
            [toggleAllSwitches, faceSwitches](bool visible) { toggleAllSwitches(faceSwitches, visible); });
        panelLayout->addWidget(facesCheckbox);

        auto* linesCheckbox = new QCheckBox(QStringLiteral("Lines"), panel);
        linesCheckbox->setChecked(true);
        auto lineSwitches = sceneData.lineSwitches;
        QObject::connect(linesCheckbox, &QCheckBox::toggled, &mainWindow,
            [toggleAllSwitches, lineSwitches](bool visible) { toggleAllSwitches(lineSwitches, visible); });
        panelLayout->addWidget(linesCheckbox);

        auto* pointsCheckbox = new QCheckBox(QStringLiteral("Points"), panel);
        pointsCheckbox->setChecked(true);
        auto pointSwitches = sceneData.pointSwitches;
        QObject::connect(pointsCheckbox, &QCheckBox::toggled, &mainWindow,
            [toggleAllSwitches, pointSwitches](bool visible) { toggleAllSwitches(pointSwitches, visible); });
        panelLayout->addWidget(pointsCheckbox);

        // Separator between display controls and part list
        auto* separator = new QFrame(panel);
        separator->setFrameShape(QFrame::HLine);
        separator->setFrameShadow(QFrame::Sunken);
        separator->setStyleSheet(QStringLiteral("QFrame { color: rgba(0, 0, 0, 60); margin: 4px 0px; }"));
        panelLayout->addWidget(separator);

        // --- Part list ---
        auto* title = new QLabel(QStringLiteral("Parts (%1)").arg(sceneData.parts.size()), panel);
        title->setObjectName(QStringLiteral("partListTitle"));
        panelLayout->addWidget(title);

        // Scrollable part list for assemblies with many parts
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
            QString label = QString::fromStdString(part.name);
            if (label.isEmpty())
            {
                label = QStringLiteral("(unnamed #%1)").arg(partIndex);
            }

            auto* checkbox = new QCheckBox(label, scrollContent);
            checkbox->setChecked(true);

            // Capture switchNode by value (ref_ptr copy is cheap)
            auto switchNode = part.switchNode;
            QObject::connect(checkbox, &QCheckBox::toggled, &mainWindow,
                [switchNode](bool visible)
                {
                    if (switchNode && !switchNode->children.empty())
                    {
                        switchNode->children.front().mask = visible ? vsg::MASK_ALL : vsg::MASK_OFF;
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

        mainWindow.statusBar()->showMessage(
            QStringLiteral("Parts: %1 | Triangles: %2 | Lines: %3 | Points: %4")
                .arg(static_cast<qulonglong>(sceneData.parts.size()))
                .arg(static_cast<qulonglong>(sceneData.totalTriangleCount))
                .arg(static_cast<qulonglong>(sceneData.totalLineSegmentCount))
                .arg(static_cast<qulonglong>(sceneData.totalPointCount)));

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
