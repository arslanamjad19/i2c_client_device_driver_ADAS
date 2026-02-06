import cv2
import numpy as np
from picamera2 import Picamera2

# ------------------------
# Utils (merged)
# ------------------------
def thresholding(img):
    hsv = cv2.cvtColor(img, cv2.COLOR_BGR2HSV)
    lowerWhite = np.array([85, 0, 0])
    upperWhite = np.array([179, 160, 255])
    maskedWhite = cv2.inRange(hsv, lowerWhite, upperWhite)
    return maskedWhite


def warpImg(img, points, w, h, inv=False):
    pts1 = np.float32(points)
    pts2 = np.float32([[0, 0], [w, 0], [0, h], [w, h]])
    matrix = cv2.getPerspectiveTransform(pts2, pts1) if inv else cv2.getPerspectiveTransform(pts1, pts2)
    return cv2.warpPerspective(img, matrix, (w, h))


def nothing(x):
    pass


def initializeTrackbars(initialVals, wT=480, hT=240):
    cv2.namedWindow("Trackbars")
    cv2.resizeWindow("Trackbars", 360, 240)
    cv2.createTrackbar("Width Top", "Trackbars", initialVals[0], wT // 2, nothing)
    cv2.createTrackbar("Height Top", "Trackbars", initialVals[1], hT, nothing)
    cv2.createTrackbar("Width Bottom", "Trackbars", initialVals[2], wT // 2, nothing)
    cv2.createTrackbar("Height Bottom", "Trackbars", initialVals[3], hT, nothing)


def valTrackbars(wT=480, hT=240):
    widthTop = cv2.getTrackbarPos("Width Top", "Trackbars")
    heightTop = cv2.getTrackbarPos("Height Top", "Trackbars")
    widthBottom = cv2.getTrackbarPos("Width Bottom", "Trackbars")
    heightBottom = cv2.getTrackbarPos("Height Bottom", "Trackbars")
    return np.float32([
        (widthTop, heightTop),
        (wT - widthTop, heightTop),
        (widthBottom, heightBottom),
        (wT - widthBottom, heightBottom)
    ])


def drawPoints(img, points):
    for pt in points:
        cv2.circle(img, (int(pt[0]), int(pt[1])), 15, (0, 0, 255), cv2.FILLED)
    return img


def getHistogram(img, minPer=0.1, display=False, minVal=0.1, region=4):
    histValues = np.sum(img if region == 1 else img[img.shape[0] // region:, :], axis=0)
    maxValue = np.max(histValues)
    threshold = minPer * maxValue
    indices = np.where(histValues >= threshold)
    basePoint = int(np.average(indices)) if indices[0].size else img.shape[1] // 2

    if display:
        imgHist = np.zeros((img.shape[0], img.shape[1], 3), np.uint8)
        for x, intensity in enumerate(histValues):
            color = (255, 0, 255) if intensity >= threshold else (0, 0, 255)
            # Fix: Ensure y-coordinate is an integer and within bounds
            height = int(intensity // 255 // region) if intensity > 0 else 0
            y_pos = max(0, min(img.shape[0] - height, img.shape[0]))
            cv2.line(imgHist, (x, img.shape[0]), (x, y_pos), color, 1)
        cv2.circle(imgHist, (basePoint, img.shape[0]), 20, (0, 255, 255), cv2.FILLED)
        return basePoint, imgHist
    return basePoint


def stackImages(scale, imgArray):
    rows = len(imgArray)
    cols = len(imgArray[0]) if isinstance(imgArray[0], list) else len(imgArray)
    firstImg = imgArray[0][0] if isinstance(imgArray[0], list) else imgArray[0]
    h, w = firstImg.shape[:2]

    def resizeAndColor(img):
        img = cv2.resize(img, (0, 0), None, scale, scale)
        return cv2.cvtColor(img, cv2.COLOR_GRAY2BGR) if len(img.shape) == 2 else img

    if isinstance(imgArray[0], list):
        rowsImgs = [np.hstack([resizeAndColor(imgArray[i][j]) for j in range(cols)]) for i in range(rows)]
        return np.vstack(rowsImgs)
    else:
        imgs = [resizeAndColor(imgArray[i]) for i in range(rows)]
        return np.hstack(imgs)

# ------------------------
# Lane Detection Module
# ------------------------
curveList = []
avgVal = 10

def getLaneCurve(img, display=2):
    imgCopy, imgResult = img.copy(), img.copy()

    # STEP 1: Thresholding
    imgThres = thresholding(img)

    # STEP 2: Perspective warp
    hT, wT = img.shape[:2]
    points = valTrackbars(wT, hT)
    imgWarp = warpImg(imgThres, points, wT, hT)
    imgWarpPoints = drawPoints(imgCopy, points)

    # STEP 3: Histogram for curve calculation
    middlePoint = getHistogram(imgWarp, minPer=0.5)
    curveAveragePoint, imgHist = getHistogram(imgWarp, display=True, minPer=0.9, region=1)
    curveRaw = curveAveragePoint - middlePoint

    # STEP 4: Average smoothing
    curveList.append(curveRaw)
    if len(curveList) > avgVal:
        curveList.pop(0)
    curve = int(sum(curveList) / len(curveList))

    # STEP 5: Display overlay
    if display != 0:
        imgInvWarp = warpImg(imgWarp, points, wT, hT, inv=True)
        imgInvWarp = cv2.cvtColor(imgInvWarp, cv2.COLOR_GRAY2BGR)
        imgInvWarp[0:hT // 3, :] = 0
        imgLaneColor = np.zeros_like(img)
        imgLaneColor[:] = (0, 255, 0)
        imgLaneColor = cv2.bitwise_and(imgInvWarp, imgLaneColor)
        imgResult = cv2.addWeighted(imgResult, 1, imgLaneColor, 1, 0)
        midY = hT // 2 + 50
        cv2.putText(imgResult, str(curve), (wT // 2 - 80, 85), cv2.FONT_HERSHEY_COMPLEX, 2, (255, 0, 255), 3)
        cv2.line(imgResult, (wT // 2, midY), (wT // 2 + curve * 3, midY), (255, 0, 255), 5)
        cv2.line(imgResult, (wT // 2 + curve * 3, midY - 25), (wT // 2 + curve * 3, midY + 25), (0, 255, 0), 5)
        for x in range(-30, 30):
            offset = int(curve / 50)
            posx = (wT // 20) * x + offset
            cv2.line(imgResult, (posx, midY - 10), (posx, midY + 10), (0, 0, 255), 2)

    if display == 2:
        imgStack = stackImages(0.7, [[img, imgWarpPoints, imgWarp], [imgHist, imgLaneColor, imgResult]])
        cv2.imshow('ImageStack', imgStack)
    elif display == 1:
        cv2.imshow('Result', imgResult)

    # Debug windows
    cv2.imshow('Thresholded', imgThres)
    cv2.imshow('Warp', imgWarp)
    cv2.imshow('Warp Points', imgWarpPoints)
    cv2.imshow('Histogram', imgHist)

    # Normalize curve
    curve_norm = max(min(curve / 100.0, 1.0), -1.0)
    return curve_norm

# ------------------------
# Main using Picamera2
# ------------------------
if __name__ == '__main__':
    initialTrackbarVals = [110, 208, 0, 480]
    initializeTrackbars(initialTrackbarVals)

    picam2 = Picamera2()
    picam2.preview_configuration.main.size = (480, 240)
    picam2.preview_configuration.main.format = "RGB888"
    picam2.start()

    try:
        while True:
            frame = picam2.capture_array()
            img = cv2.cvtColor(frame, cv2.COLOR_RGB2BGR)
            _ = getLaneCurve(img, display=2)
            if cv2.waitKey(1) & 0xFF == ord('q'):
                break
    finally:
        picam2.stop()
        cv2.destroyAllWindows()