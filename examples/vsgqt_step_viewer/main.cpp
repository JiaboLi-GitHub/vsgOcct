#include <vsgocct/ModelLoader.h>
#include <vsgocct/selection/ScenePicker.h>

#include <QtCore/QFileInfo>
#include <QtCore/QStringList>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDockWidget>
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
#include <optional>

namespace
{
bool hasOption(const QStringList& arguments, const QString& option)
{
    return arguments.contains(option, Qt::CaseInsensitive);
}

QString resolveModelFile(const QStringList& arguments)
{
    for (int index = 1; index < arguments.size(); ++index)
    {
        const QString argument = arguments.at(index);
        if (!argument.startsWith('-'))
        {
            return argument;
        }
    }

    const QString initialDirectory = std::filesystem::exists("D:/OCCT/data/step")
                                         ? QStringLiteral("D:/OCCT/data/step")
                                         : QString();

    return QFileDialog::getOpenFileName(
        nullptr,
        QStringLiteral("Open 3D Model"),
        initialDirectory,
        QStringLiteral("3D Models (*.step *.stp *.stl *.STEP *.STP *.STL);;STEP Files (*.step *.stp);;STL Files (*.stl);;All Files (*.*)"));
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

QString shadingModeLabel(vsgocct::scene::ShadingMode shadingMode)
{
    return shadingMode == vsgocct::scene::ShadingMode::Pbr
               ? QStringLiteral("PBR")
               : QStringLiteral("Legacy");
}

QString materialPresetLabel(vsgocct::scene::MaterialPreset preset)
{
    using vsgocct::scene::MaterialPreset;

    switch (preset)
    {
    case MaterialPreset::Imported:
        return QStringLiteral("原始材质");
    case MaterialPreset::Iron:
        return QStringLiteral("铁");
    case MaterialPreset::Copper:
        return QStringLiteral("铜");
    case MaterialPreset::Gold:
        return QStringLiteral("黄金");
    case MaterialPreset::Wood:
        return QStringLiteral("木头");
    case MaterialPreset::Acrylic:
        return QStringLiteral("亚克力");
    default:
        return QStringLiteral("原始材质");
    }
}

QString materialPresetDescription(vsgocct::scene::MaterialPreset preset,
                                  vsgocct::scene::ShadingMode shadingMode)
{
    using vsgocct::scene::MaterialPreset;

    QString suffix;
    if (shadingMode == vsgocct::scene::ShadingMode::Legacy)
    {
        suffix = QStringLiteral("Legacy 模式下主要体现基础色，完整材质观感建议使用 PBR。");
    }

    switch (preset)
    {
    case MaterialPreset::Imported:
        return suffix.isEmpty()
                   ? QStringLiteral("使用 STEP/XCAF 导入的原始颜色或材质。")
                   : QStringLiteral("使用 STEP/XCAF 导入的原始颜色或材质。%1").arg(suffix);
    case MaterialPreset::Iron:
        return suffix.isEmpty()
                   ? QStringLiteral("冷灰色金属，较高金属度，中等粗糙度。")
                   : QStringLiteral("冷灰色金属，较高金属度，中等粗糙度。%1").arg(suffix);
    case MaterialPreset::Copper:
        return suffix.isEmpty()
                   ? QStringLiteral("偏暖红铜色，高金属度，带轻微抛光感。")
                   : QStringLiteral("偏暖红铜色，高金属度，带轻微抛光感。%1").arg(suffix);
    case MaterialPreset::Gold:
        return suffix.isEmpty()
                   ? QStringLiteral("高反射暖金属色，低粗糙度。")
                   : QStringLiteral("高反射暖金属色，低粗糙度。%1").arg(suffix);
    case MaterialPreset::Wood:
        return suffix.isEmpty()
                   ? QStringLiteral("低金属度木质表面，棕色基底，较高粗糙度。")
                   : QStringLiteral("低金属度木质表面，棕色基底，较高粗糙度。%1").arg(suffix);
    case MaterialPreset::Acrylic:
        return suffix.isEmpty()
                   ? QStringLiteral("半透明高光塑料感材质，适合观察透光效果。")
                   : QStringLiteral("半透明高光塑料感材质，适合观察透光效果。%1").arg(suffix);
    default:
        return suffix;
    }
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

bool sameToken(const vsgocct::selection::SelectionToken& lhs,
               const vsgocct::selection::SelectionToken& rhs)
{
    return lhs.partId == rhs.partId &&
           lhs.kind == rhs.kind &&
           lhs.primitiveId == rhs.primitiveId;
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
                          QString shadingLabel,
                          const QString* materialLabel,
                          QStatusBar* statusBar)
        : viewer_(std::move(viewer)),
          camera_(std::move(camera)),
          sceneData_(sceneData),
          shadingLabel_(std::move(shadingLabel)),
          materialLabel_(materialLabel),
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
        syncStatusAndRefresh(clearHoverState());
    }

    void apply(vsg::MoveEvent& moveEvent) override
    {
        if (moveEvent.mask != vsg::BUTTON_MASK_OFF || pressed_)
        {
            syncStatusAndRefresh(clearHoverState());
            return;
        }

        updateHover(moveEvent.x, moveEvent.y);
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
            updateHover(buttonRelease.x, buttonRelease.y);
            return;
        }

        const bool hoverChanged = clearHoverState();
        const auto pickResult = vsgocct::selection::pick(*camera_, sceneData_, buttonRelease.x, buttonRelease.y);
        if (!pickResult)
        {
            const bool selectionChanged = clearSelectionState();
            syncStatusAndRefresh(hoverChanged || selectionChanged);
            return;
        }

        const bool tokenChanged = !sameToken(sceneData_.selectedToken, pickResult->token);
        if (!vsgocct::scene::setSelection(sceneData_, pickResult->token))
        {
            syncStatusAndRefresh(hoverChanged);
            return;
        }

        selectedPick_ = *pickResult;
        syncStatusAndRefresh(hoverChanged || tokenChanged);
    }

    void showDefaultStatus() const
    {
        refreshStatus();
    }

    void clearSelection()
    {
        syncStatusAndRefresh(clearSelectionState());
    }

    void clearHover()
    {
        syncStatusAndRefresh(clearHoverState());
    }

    void clearPartState(uint32_t partId)
    {
        bool changed = false;

        if (sceneData_.selectedToken && sceneData_.selectedToken.partId == partId)
        {
            changed = clearSelectionState() || changed;
        }

        if (sceneData_.hoverToken && sceneData_.hoverToken.partId == partId)
        {
            changed = clearHoverState() || changed;
        }

        syncStatusAndRefresh(changed);
    }

    void requestRefresh() const
    {
        if (viewer_)
        {
            viewer_->request();
        }
    }

private:
    QString describePick(const QString& prefix, const vsgocct::selection::PickResult& pickResult) const
    {
        const auto* part = vsgocct::scene::findPart(sceneData_, pickResult.token.partId);
        const QString label = part
                                  ? partLabel(*part, static_cast<int>(pickResult.token.partId + 1))
                                  : QStringLiteral("(unknown part)");
        return QStringLiteral("%1 %2 %3 | %4")
            .arg(prefix)
            .arg(primitiveKindLabel(pickResult.token.kind))
            .arg(static_cast<qulonglong>(pickResult.token.primitiveId))
            .arg(label);
    }

    void refreshStatus() const
    {
        if (!statusBar_)
        {
            return;
        }

        if (hoverPick_)
        {
            QString message = QStringLiteral("%1 | Hit (%2, %3, %4)")
                .arg(describePick(QStringLiteral("Hover"), *hoverPick_))
                .arg(hoverPick_->worldIntersection.x, 0, 'f', 3)
                .arg(hoverPick_->worldIntersection.y, 0, 'f', 3)
                .arg(hoverPick_->worldIntersection.z, 0, 'f', 3);

            if (selectedPick_ && !sameToken(selectedPick_->token, hoverPick_->token))
            {
                message += QStringLiteral(" | %1")
                    .arg(describePick(QStringLiteral("Selected"), *selectedPick_));
            }

            statusBar_->showMessage(message);
            return;
        }

        if (selectedPick_)
        {
            statusBar_->showMessage(
                QStringLiteral("%1 | Hit (%2, %3, %4)")
                    .arg(describePick(QStringLiteral("Selected"), *selectedPick_))
                    .arg(selectedPick_->worldIntersection.x, 0, 'f', 3)
                    .arg(selectedPick_->worldIntersection.y, 0, 'f', 3)
                    .arg(selectedPick_->worldIntersection.z, 0, 'f', 3));
            return;
        }

        statusBar_->showMessage(
            QStringLiteral("Shading: %1 | Material: %2 | %3")
                .arg(shadingLabel_)
                .arg(materialLabel_ ? *materialLabel_ : QStringLiteral("N/A"))
                .arg(defaultStatusMessage(sceneData_)));
    }

    void syncStatusAndRefresh(bool changed)
    {
        refreshStatus();
        if (changed)
        {
            requestRefresh();
        }
    }

    bool clearSelectionState()
    {
        if (!sceneData_.selectedToken && !selectedPick_)
        {
            return false;
        }

        vsgocct::scene::clearSelection(sceneData_);
        selectedPick_.reset();
        return true;
    }

    bool clearHoverState()
    {
        if (!sceneData_.hoverToken && !hoverPick_)
        {
            return false;
        }

        vsgocct::scene::clearHoverSelection(sceneData_);
        hoverPick_.reset();
        return true;
    }

    void updateHover(int32_t x, int32_t y)
    {
        const auto pickResult = vsgocct::selection::pick(*camera_, sceneData_, x, y);
        if (!pickResult || sameToken(pickResult->token, sceneData_.selectedToken))
        {
            syncStatusAndRefresh(clearHoverState());
            return;
        }

        const bool tokenChanged = !sameToken(sceneData_.hoverToken, pickResult->token);
        if (tokenChanged)
        {
            if (!vsgocct::scene::setHoverSelection(sceneData_, pickResult->token))
            {
                syncStatusAndRefresh(clearHoverState());
                return;
            }
        }

        hoverPick_ = *pickResult;
        syncStatusAndRefresh(tokenChanged);
    }

    vsg::ref_ptr<vsgQt::Viewer> viewer_;
    vsg::ref_ptr<vsg::Camera> camera_;
    vsgocct::scene::AssemblySceneData& sceneData_;
    QString shadingLabel_;
    const QString* materialLabel_ = nullptr;
    QStatusBar* statusBar_ = nullptr;
    std::optional<vsgocct::selection::PickResult> selectedPick_;
    std::optional<vsgocct::selection::PickResult> hoverPick_;
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

        const QString modelFile = resolveModelFile(application.arguments());
        if (modelFile.isEmpty())
        {
            return 0;
        }

        vsgocct::scene::SceneOptions sceneOptions;
        sceneOptions.shadingMode = hasOption(application.arguments(), QStringLiteral("--legacy"))
                                       ? vsgocct::scene::ShadingMode::Legacy
                                       : vsgocct::scene::ShadingMode::Pbr;
        const QString shadingLabel = shadingModeLabel(sceneOptions.shadingMode);

        auto sceneData = vsgocct::loadScene(
            std::filesystem::path(modelFile.toStdWString()),
            sceneOptions);
        std::cout << "Loaded " << sceneData.totalPointCount << " points, "
                  << sceneData.totalLineSegmentCount << " line segments, "
                  << sceneData.totalTriangleCount << " triangles, "
                  << sceneData.parts.size() << " parts in " << shadingLabel.toStdString() << " mode from "
                  << modelFile.toLocal8Bit().constData() << std::endl;

        auto viewer = vsgQt::Viewer::create();

        auto traits = vsg::WindowTraits::create();
        traits->windowTitle = QStringLiteral("vsgQt OCCT STEP Viewer (%1)").arg(shadingLabel).toStdString();
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

        mainWindow.setWindowTitle(
            QStringLiteral("STEP Viewer (%1) - %2")
                .arg(shadingLabel)
                .arg(QFileInfo(modelFile).fileName()));
        mainWindow.setCentralWidget(centralBase);
        mainWindow.resize(static_cast<int>(traits->width), static_cast<int>(traits->height));

        QString activeMaterialLabel = materialPresetLabel(sceneData.materialPreset);

        auto selectionHandler = vsg::ref_ptr<SelectionClickHandler>(
            new SelectionClickHandler(
                viewer,
                renderWindowContext.camera,
                sceneData,
                shadingLabel,
                &activeMaterialLabel,
                mainWindow.statusBar()));
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

                    if (!visible &&
                        ((sceneData.selectedToken && sceneData.selectedToken.partId == partId) ||
                         (sceneData.hoverToken && sceneData.hoverToken.partId == partId)))
                    {
                        selectionHandler->clearPartState(partId);
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

        auto* materialDock = new QDockWidget(QStringLiteral("材质"), &mainWindow);
        materialDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        materialDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
        materialDock->setMinimumWidth(250);

        auto* materialPanel = new QWidget(materialDock);
        auto* materialLayout = new QVBoxLayout(materialPanel);
        materialLayout->setContentsMargins(10, 10, 10, 10);
        materialLayout->setSpacing(8);

        auto* materialTitle = new QLabel(QStringLiteral("整模型材质"), materialPanel);
        materialTitle->setStyleSheet(QStringLiteral("font-weight: 600;"));
        materialLayout->addWidget(materialTitle);

        auto* materialCombo = new QComboBox(materialPanel);
        for (const auto preset : {vsgocct::scene::MaterialPreset::Imported,
                                  vsgocct::scene::MaterialPreset::Iron,
                                  vsgocct::scene::MaterialPreset::Copper,
                                  vsgocct::scene::MaterialPreset::Gold,
                                  vsgocct::scene::MaterialPreset::Wood,
                                  vsgocct::scene::MaterialPreset::Acrylic})
        {
            materialCombo->addItem(
                materialPresetLabel(preset),
                static_cast<int>(preset));
        }
        materialLayout->addWidget(materialCombo);

        auto* materialHint = new QLabel(
            materialPresetDescription(sceneData.materialPreset, sceneData.shadingMode),
            materialPanel);
        materialHint->setWordWrap(true);
        materialHint->setStyleSheet(QStringLiteral("color: palette(mid);"));
        materialLayout->addWidget(materialHint);
        materialLayout->addStretch();

        QObject::connect(materialCombo, &QComboBox::currentIndexChanged, &mainWindow,
            [&sceneData, selectionHandler, &activeMaterialLabel, materialCombo, materialHint](int index)
            {
                const auto preset = static_cast<vsgocct::scene::MaterialPreset>(
                    materialCombo->itemData(index).toInt());
                activeMaterialLabel = materialPresetLabel(preset);
                materialHint->setText(materialPresetDescription(preset, sceneData.shadingMode));

                if (vsgocct::scene::applyMaterialPreset(sceneData, preset))
                {
                    selectionHandler->showDefaultStatus();
                    selectionHandler->requestRefresh();
                }
                else
                {
                    selectionHandler->showDefaultStatus();
                }
            });

        materialDock->setWidget(materialPanel);
        mainWindow.addDockWidget(Qt::RightDockWidgetArea, materialDock);

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
