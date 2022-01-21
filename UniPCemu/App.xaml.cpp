//
// App.xaml.cpp
// Реализация класса приложения.
//

#include "pch.h"
#include "MainPage.xaml.h"

using namespace UniPCemu;

using namespace Platform;
using namespace Windows::ApplicationModel;
using namespace Windows::ApplicationModel::Activation;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Controls::Primitives;
using namespace Windows::UI::Xaml::Data;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Interop;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Navigation;

/// <summary>
/// Инициализирует одноэлементный объект приложения.  Это первая выполняемая строка разрабатываемого
/// кода; поэтому она является логическим эквивалентом main() или WinMain().
/// </summary>
App::App()
{
    InitializeComponent();
    Suspending += ref new SuspendingEventHandler(this, &App::OnSuspending);
}

/// <summary>
/// Вызывается при обычном запуске приложения пользователем. Будут использоваться другие точки входа,
/// например, если приложение запускается для открытия конкретного файла.
/// </summary>
/// <param name="e">Сведения о запросе и обработке запуска.</param>
void App::OnLaunched(Windows::ApplicationModel::Activation::LaunchActivatedEventArgs^ e)
{
    auto rootFrame = dynamic_cast<Frame^>(Window::Current->Content);

    // Не повторяйте инициализацию приложения, если в окне уже имеется содержимое,
    // только обеспечьте активность окна
    if (rootFrame == nullptr)
    {
        // Создайте фрейм, используемый в качестве контекста навигации, и свяжите его с
        // ключом SuspensionManager
        rootFrame = ref new Frame();

        rootFrame->NavigationFailed += ref new Windows::UI::Xaml::Navigation::NavigationFailedEventHandler(this, &App::OnNavigationFailed);

        if (e->PreviousExecutionState == ApplicationExecutionState::Terminated)
        {
            // TODO: Восстановление сохраненного состояния сеанса только при необходимости и планирование
            // финальных шагов запуска после завершения восстановления

        }

        if (e->PrelaunchActivated == false)
        {
            if (rootFrame->Content == nullptr)
            {
                // Если стек навигации не восстанавливается для перехода к первой странице,
                // настройка новой страницы путем передачи необходимой информации в качестве параметра
                // параметр
                rootFrame->Navigate(TypeName(MainPage::typeid), e->Arguments);
            }
            // Размещение фрейма в текущем окне
            Window::Current->Content = rootFrame;
            // Обеспечение активности текущего окна
            Window::Current->Activate();
        }
    }
    else
    {
        if (e->PrelaunchActivated == false)
        {
            if (rootFrame->Content == nullptr)
            {
                // Если стек навигации не восстанавливается для перехода к первой странице,
                // настройка новой страницы путем передачи необходимой информации в качестве параметра
                // параметр
                rootFrame->Navigate(TypeName(MainPage::typeid), e->Arguments);
            }
            // Обеспечение активности текущего окна
            Window::Current->Activate();
        }
    }
}

/// <summary>
/// Вызывается при приостановке выполнения приложения.  Состояние приложения сохраняется
/// без учета информации о том, будет ли оно завершено или возобновлено с неизменным
/// содержимым памяти.
/// </summary>
/// <param name="sender">Источник запроса приостановки.</param>
/// <param name="e">Сведения о запросе приостановки.</param>
void App::OnSuspending(Object^ sender, SuspendingEventArgs^ e)
{
    (void) sender;  // Неиспользуемый параметр
    (void) e;   // Неиспользуемый параметр

    //TODO: Сохранить состояние приложения и остановить все фоновые операции
}

/// <summary>
/// Вызывается в случае сбоя навигации на определенную страницу
/// </summary>
/// <param name="sender">Фрейм, для которого произошел сбой навигации</param>
/// <param name="e">Сведения о сбое навигации</param>
void App::OnNavigationFailed(Platform::Object ^sender, Windows::UI::Xaml::Navigation::NavigationFailedEventArgs ^e)
{
    throw ref new FailureException("Failed to load Page " + e->SourcePageType.Name);
}