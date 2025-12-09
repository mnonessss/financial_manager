#include <drogon/drogon.h>

int main() {
    // Загружаем конфиг (БД, логирование, слушатели и т.п.)
    drogon::app().loadConfigFile("../config.json");
    // Если нужно использовать YAML вместо JSON, можно раскомментировать следующую строку:
    // drogon::app().loadConfigFile("../config.yaml");

    // Если в конфиге уже есть listeners, эту строку можно не вызывать,
    // но она не мешает и переопределяет адрес/порт при необходимости.
    drogon::app().addListener("0.0.0.0", 9000);

    drogon::app().run();
    return 0;
}
