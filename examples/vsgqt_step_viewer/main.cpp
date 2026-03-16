#include <vsgocct/StepModelLoader.h>
#include <vsgocct/pick/PickHandler.h>
#include <vsgocct/pick/SelectionManager.h>

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

struct OffscreenResources
{
    vsg::ref_ptr<vsg::Image> colorImage;
    vsg::ref_ptr<vsg::Framebuffer> framebuffer;
    vsg::ref_ptr<vsg::RenderPass> renderPass;
};

OffscreenResources createOffscreenFramebuffer(vsg::ref_ptr<vsg::Device> device, uint32_t width, uint32_t height)
{
    // Color attachment
    auto colorImage = vsg::Image::create();
    colorImage->imageType = VK_IMAGE_TYPE_2D;
    colorImage->format = VK_FORMAT_R8G8B8A8_UNORM;
    colorImage->extent = {width, height, 1};
    colorImage->mipLevels = 1;
    colorImage->arrayLayers = 1;
    colorImage->samples = VK_SAMPLE_COUNT_1_BIT;
    colorImage->tiling = VK_IMAGE_TILING_OPTIMAL;
    colorImage->usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    colorImage->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorImage->sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    auto colorImageView = vsg::ImageView::create(colorImage, VK_IMAGE_ASPECT_COLOR_BIT);

    // Depth attachment
    auto depthImage = vsg::Image::create();
    depthImage->imageType = VK_IMAGE_TYPE_2D;
    depthImage->format = VK_FORMAT_D32_SFLOAT;
    depthImage->extent = {width, height, 1};
    depthImage->mipLevels = 1;
    depthImage->arrayLayers = 1;
    depthImage->samples = VK_SAMPLE_COUNT_1_BIT;
    depthImage->tiling = VK_IMAGE_TILING_OPTIMAL;
    depthImage->usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depthImage->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthImage->sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    auto depthImageView = vsg::ImageView::create(depthImage, VK_IMAGE_ASPECT_DEPTH_BIT);

    // Render pass
    vsg::AttachmentDescription colorAttachment = {};
    colorAttachment.flags = 0;
    colorAttachment.format = VK_FORMAT_R8G8B8A8_UNORM;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    vsg::AttachmentDescription depthAttachment = {};
    depthAttachment.flags = 0;
    depthAttachment.format = VK_FORMAT_D32_SFLOAT;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    vsg::AttachmentReference colorRef = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    vsg::AttachmentReference depthRef = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    vsg::SubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachments = {colorRef};
    subpass.depthStencilAttachments = {depthRef};

    auto renderPass = vsg::RenderPass::create(device, vsg::RenderPass::Attachments{colorAttachment, depthAttachment},
                                               vsg::RenderPass::Subpasses{subpass},
                                               vsg::RenderPass::Dependencies{});

    auto framebuffer = vsg::Framebuffer::create(renderPass, vsg::ImageViews{colorImageView, depthImageView}, width, height, 1);

    return {colorImage, framebuffer, renderPass};
}

vsgQt::Window* createRenderWindow(
    const vsg::ref_ptr<vsgQt::Viewer>& viewer,
    const vsg::ref_ptr<vsg::WindowTraits>& traits,
    const vsgocct::scene::AssemblySceneData& sceneData,
    vsg::ref_ptr<vsg::Image>& outPickImage)
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

    // Create offscreen pick framebuffer
    auto offscreen = createOffscreenFramebuffer(traits->device, width, height);
    outPickImage = offscreen.colorImage;

    // Create pick RenderGraph targeting the offscreen framebuffer
    auto pickRenderGraph = vsg::RenderGraph::create();
    pickRenderGraph->framebuffer = offscreen.framebuffer;
    pickRenderGraph->renderArea = {{0, 0}, {width, height}};
    pickRenderGraph->clearValues = {
        {{0.0f, 0.0f, 0.0f, 0.0f}},
        {{1.0f, 0}}
    };
    pickRenderGraph->addChild(vsg::View::create(camera, sceneData.pickScene));

    auto pickCommandGraph = vsg::CommandGraph::create(*window);
    pickCommandGraph->addChild(pickRenderGraph);

    viewer->addRecordAndSubmitTaskAndPresentation({commandGraph, pickCommandGraph});

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
        vsg::ref_ptr<vsg::Image> pickImage;
        auto* renderWindow = createRenderWindow(viewer, traits, sceneData, pickImage);

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

        auto selectionManager = std::make_shared<vsgocct::pick::SelectionManager>();

        auto pickHandler = vsgocct::pick::PickHandler::create(pickImage, traits->device, sceneData);

        viewer->addEventHandler(pickHandler);

        // --- Floating overlay: part list with visibility toggles ---
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

            // Capture switchNode and pickSwitchNode by value (ref_ptr copy is cheap)
            auto switchNode = part.switchNode;
            auto pickSwitchNode = part.pickSwitchNode;
            QObject::connect(checkbox, &QCheckBox::toggled, &mainWindow,
                [switchNode, pickSwitchNode](bool visible)
                {
                    auto mask = visible ? vsg::MASK_ALL : vsg::MASK_OFF;
                    if (switchNode && !switchNode->children.empty())
                    {
                        switchNode->children.front().mask = mask;
                    }
                    if (pickSwitchNode && !pickSwitchNode->children.empty())
                    {
                        pickSwitchNode->children.front().mask = mask;
                    }
                });

            scrollLayout->addWidget(checkbox);
        }
        scrollLayout->addStretch();

        scrollArea->setWidget(scrollContent);
        scrollArea->setMaximumHeight(350);
        panelLayout->addWidget(scrollArea);

        auto* separator = new QFrame(panel);
        separator->setFrameShape(QFrame::HLine);
        separator->setStyleSheet("color: rgba(255,255,255,30);");
        panelLayout->addWidget(separator);

        auto* infoTitle = new QLabel(QStringLiteral("Selected Face"), panel);
        infoTitle->setObjectName(QStringLiteral("partListTitle"));
        auto* faceIdLabel = new QLabel(panel);
        auto* partNameLabel = new QLabel(panel);
        auto* normalLabel = new QLabel(panel);
        faceIdLabel->setStyleSheet("color: rgba(255,255,255,180); font-size: 12px; padding: 2px 6px;");
        partNameLabel->setStyleSheet("color: rgba(255,255,255,180); font-size: 12px; padding: 2px 6px;");
        normalLabel->setStyleSheet("color: rgba(255,255,255,180); font-size: 12px; padding: 2px 6px;");

        panelLayout->addWidget(infoTitle);
        panelLayout->addWidget(faceIdLabel);
        panelLayout->addWidget(partNameLabel);
        panelLayout->addWidget(normalLabel);

        // Initially hidden
        infoTitle->setVisible(false);
        separator->setVisible(false);
        faceIdLabel->setVisible(false);
        partNameLabel->setVisible(false);
        normalLabel->setVisible(false);

        pickHandler->setPickCallback(
            [selectionManager, &sceneData, infoTitle, separator, faceIdLabel, partNameLabel, normalLabel,
             statusBar = mainWindow.statusBar()](const vsgocct::scene::PickResult& result)
            {
                selectionManager->selectFace(result.faceId, sceneData);
                bool hasFace = result.faceInfo != nullptr;

                infoTitle->setVisible(hasFace);
                separator->setVisible(hasFace);
                faceIdLabel->setVisible(hasFace);
                partNameLabel->setVisible(hasFace);
                normalLabel->setVisible(hasFace);

                if (hasFace)
                {
                    faceIdLabel->setText(QStringLiteral("Face ID: %1").arg(result.faceId));
                    partNameLabel->setText(QStringLiteral("Part: %1").arg(
                        QString::fromStdString(result.faceInfo->partName)));
                    normalLabel->setText(QStringLiteral("Normal: (%1, %2, %3)")
                        .arg(result.faceInfo->faceNormal.x, 0, 'f', 2)
                        .arg(result.faceInfo->faceNormal.y, 0, 'f', 2)
                        .arg(result.faceInfo->faceNormal.z, 0, 'f', 2));
                    statusBar->showMessage(
                        QStringLiteral("Face %1 | Part: %2 | Normal: (%3, %4, %5)")
                            .arg(result.faceId)
                            .arg(QString::fromStdString(result.faceInfo->partName))
                            .arg(result.faceInfo->faceNormal.x, 0, 'f', 2)
                            .arg(result.faceInfo->faceNormal.y, 0, 'f', 2)
                            .arg(result.faceInfo->faceNormal.z, 0, 'f', 2));
                }
                else
                {
                    selectionManager->clearSelection();
                    statusBar->showMessage(QStringLiteral("Parts: %1 | Triangles: %2 | Lines: %3 | Points: %4")
                        .arg(static_cast<qulonglong>(sceneData.parts.size()))
                        .arg(static_cast<qulonglong>(sceneData.totalTriangleCount))
                        .arg(static_cast<qulonglong>(sceneData.totalLineSegmentCount))
                        .arg(static_cast<qulonglong>(sceneData.totalPointCount)));
                }
            });

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
