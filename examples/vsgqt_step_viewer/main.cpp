#include <vsgocct/StepModelLoader.h>

#include <QtGui/QAction>
#include <QtCore/QFileInfo>
#include <QtCore/QStringList>
#include <QtWidgets/QApplication>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QToolBar>
#include <QtWidgets/QWidget>

#include <vsgQt/Viewer.h>
#include <vsgQt/Window.h>

#include <algorithm>
#include <filesystem>
#include <iostream>

namespace
{
// 优先从命令行读取 STEP 文件路径；
// 如果没有传参，则弹出文件选择框，方便双击运行示例时直接选模型。
QString resolveStepFile(const QStringList& arguments)
{
    if (arguments.size() > 1)
    {
        return arguments.at(1);
    }

    // 如果本机存在 OCCT 自带示例数据目录，就把它作为初始位置，
    // 这样在开发环境里测试会更顺手；否则退回到 Qt 默认目录。
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
    const vsgocct::StepSceneData& sceneData)
{
    // 创建底层 Vulkan 渲染窗口，并把它包装成可嵌入 Qt 的 QWindow。
    auto* window = new vsgQt::Window(viewer, traits, static_cast<QWindow*>(nullptr));
    window->setTitle(QString::fromStdString(traits->windowTitle));
    window->initializeWindow();

    if (!traits->device)
    {
        // 若调用方还没有预先创建 Vulkan 设备，则直接复用窗口适配器生成的默认设备。
        traits->device = window->windowAdapter->getOrCreateDevice();
    }

    const auto width = traits->width;
    const auto height = traits->height;
    const auto radius = std::max(sceneData.radius, 1.0);
    const auto centre = sceneData.center;

    // 根据模型包围球设置一个“能完整看到模型”的默认观察位置：
    // 相机位于模型前上方，既能看到整体轮廓，也能保留一定立体感。
    auto lookAt = vsg::LookAt::create(
        centre + vsg::dvec3(0.0, -radius * 3.0, radius * 1.6),
        centre,
        vsg::dvec3(0.0, 0.0, 1.0));
    // 近平面按模型半径的千分之一估算，避免缩放时过早裁切；
    // 远平面则留出更大的余量，兼顾大模型显示稳定性。
    auto projection = vsg::Perspective::create(
        35.0,
        static_cast<double>(width) / static_cast<double>(height),
        std::max(radius * 0.001, 0.001),
        radius * 12.0);
    auto camera = vsg::Camera::create(projection, lookAt, vsg::ViewportState::create(VkExtent2D{width, height}));

    // 绑定轨迹球控制器，让用户可以旋转、平移和缩放模型。
    auto trackball = vsg::Trackball::create(camera);
    trackball->addWindow(*window);

    viewer->addEventHandler(trackball);
    viewer->addEventHandler(vsg::CloseHandler::create(viewer));

    // CommandGraph 把窗口、相机和场景串起来，形成一套可提交到 Vulkan 队列的绘制任务。
    auto commandGraph = vsg::createCommandGraphForView(*window, camera, sceneData.scene);
    viewer->addRecordAndSubmitTaskAndPresentation({commandGraph});

    return window;
}
} // namespace

int main(int argc, char* argv[])
{
    try
    {
        // Qt 应用对象负责消息循环、窗口系统和控件生命周期。
        QApplication application(argc, argv);

        const QString stepFile = resolveStepFile(application.arguments());
        if (stepFile.isEmpty())
        {
            // 用户取消选文件时直接正常退出，不视为错误。
            return 0;
        }

        // 加载 STEP 模型并转换为 VSG 场景，同时输出点线面统计供终端调试。
        auto sceneData = vsgocct::loadStepScene(std::filesystem::path(stepFile.toStdWString()));
        std::cout << "Loaded " << sceneData.pointCount << " points, "
                  << sceneData.lineSegmentCount << " line segments, "
                  << sceneData.triangleCount << " triangles from "
                  << stepFile.toLocal8Bit().constData() << std::endl;

        auto viewer = vsgQt::Viewer::create();

        auto traits = vsg::WindowTraits::create();
        traits->windowTitle = "vsgQt OCCT STEP Viewer";
        traits->width = 1280;
        traits->height = 900;
        traits->samples = VK_SAMPLE_COUNT_4_BIT;

        // 用标准 QMainWindow 承载渲染窗口，这样后续很容易继续扩展菜单栏、工具栏和状态栏。
        QMainWindow mainWindow;
        auto* renderWindow = createRenderWindow(viewer, traits, sceneData);
        auto* container = QWidget::createWindowContainer(renderWindow, &mainWindow);
        container->setFocusPolicy(Qt::StrongFocus);

        mainWindow.setWindowTitle(QStringLiteral("STEP Viewer - %1").arg(QFileInfo(stepFile).fileName()));
        mainWindow.setCentralWidget(container);
        mainWindow.resize(static_cast<int>(traits->width), static_cast<int>(traits->height));

        // 工具栏上的三个开关分别直连到底层 StepSceneData 的三个 Switch。
        // 这样示例层不需要了解场景树内部结构，只负责把 UI 状态同步给渲染层。
        auto* primitiveToolBar = mainWindow.addToolBar(QStringLiteral("Primitives"));
        primitiveToolBar->setMovable(false);

        auto* pointsAction = primitiveToolBar->addAction(QStringLiteral("Points"));
        pointsAction->setCheckable(true);
        pointsAction->setChecked(sceneData.pointsVisible());
        // 如果模型里没有点拓扑，就把按钮禁用，避免用户误以为切换失效。
        pointsAction->setEnabled(sceneData.pointCount > 0);
        QObject::connect(pointsAction, &QAction::toggled, &mainWindow, [&sceneData](bool visible)
        {
            sceneData.setPointsVisible(visible);
        });

        auto* linesAction = primitiveToolBar->addAction(QStringLiteral("Lines"));
        linesAction->setCheckable(true);
        linesAction->setChecked(sceneData.linesVisible());
        // 线段数量来自 Edge 提取后的逻辑统计，不依赖具体 GPU 缓冲展开方式。
        linesAction->setEnabled(sceneData.lineSegmentCount > 0);
        QObject::connect(linesAction, &QAction::toggled, &mainWindow, [&sceneData](bool visible)
        {
            sceneData.setLinesVisible(visible);
        });

        auto* facesAction = primitiveToolBar->addAction(QStringLiteral("Faces"));
        facesAction->setCheckable(true);
        facesAction->setChecked(sceneData.facesVisible());
        // 面是三角化结果，因此这里用 triangleCount 判断按钮是否可用。
        facesAction->setEnabled(sceneData.triangleCount > 0);
        QObject::connect(facesAction, &QAction::toggled, &mainWindow, [&sceneData](bool visible)
        {
            sceneData.setFacesVisible(visible);
        });

        // 状态栏同时展示三类 primitive 的数量，便于快速确认模型当前提取了哪些拓扑信息。
        mainWindow.statusBar()->showMessage(QStringLiteral("Points: %1 | Line Segments: %2 | Triangles: %3")
                                                .arg(static_cast<qulonglong>(sceneData.pointCount))
                                                .arg(static_cast<qulonglong>(sceneData.lineSegmentCount))
                                                .arg(static_cast<qulonglong>(sceneData.triangleCount)));

        // 持续重绘适合交互式三维查看器；16ms 间隔约等于 60 FPS 刷新节奏。
        viewer->continuousUpdate = true;
        viewer->setInterval(16);
        // compile 会在真正进入事件循环前完成渲染资源准备，减少首帧卡顿。
        viewer->compile();

        mainWindow.show();

        return application.exec();
    }
    catch (const vsg::Exception& ex)
    {
        // VSG 异常通常带有 Vulkan 返回码，保留 result 便于定位图形后端问题。
        std::cerr << "VSG exception: " << ex.message << " (result=" << ex.result << ')' << std::endl;
        QMessageBox::critical(nullptr, QStringLiteral("VSG Error"), QString::fromLocal8Bit(ex.message.c_str()));
        return 1;
    }
    catch (const std::exception& ex)
    {
        // 兜底捕获标准异常，避免程序静默退出，并把错误同步显示到 GUI 和终端。
        std::cerr << "Unhandled exception: " << ex.what() << std::endl;
        QMessageBox::critical(nullptr, QStringLiteral("STEP Viewer Error"), QString::fromLocal8Bit(ex.what()));
        return 1;
    }
}
